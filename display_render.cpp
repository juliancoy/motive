#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>

#include <array>
#include <chrono>
#include <cstring>
#include <glm/geometric.hpp>
#include <iostream>
#include <mutex>
#include <stdexcept>

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "primitive.h"
#include "model.h"

namespace
{
struct Frustum
{
    std::array<glm::vec4, 6> planes;
};

Frustum extractFrustum(const glm::mat4& vp)
{
    Frustum f;
    const auto row = [&vp](int i) {
        return glm::vec4(vp[0][i], vp[1][i], vp[2][i], vp[3][i]);
    };

    glm::vec4 r0 = row(0);
    glm::vec4 r1 = row(1);
    glm::vec4 r2 = row(2);
    glm::vec4 r3 = row(3);

    f.planes[0] = r3 + r0;
    f.planes[1] = r3 - r0;
    f.planes[2] = r3 + r1;
    f.planes[3] = r3 - r1;
    f.planes[4] = r2;
    f.planes[5] = r3 - r2;

    for (auto& p : f.planes)
    {
        float len = glm::length(glm::vec3(p));
        if (len > 0.0f)
        {
            p /= len;
        }
    }
    return f;
}

bool sphereInFrustum(const Frustum& f, const glm::vec3& center, float radius)
{
    for (const auto& p : f.planes)
    {
        float dist = glm::dot(glm::vec3(p), center) + p.w;
        if (dist < -radius)
        {
            return false;
        }
    }
    return true;
}

uint32_t pipelineIndexForCullMode(PrimitiveCullMode mode, bool cullingDisabled, bool use2DPipeline)
{
    if (cullingDisabled || use2DPipeline)
    {
        return static_cast<uint32_t>(PrimitiveCullMode::Disabled);
    }
    return static_cast<uint32_t>(mode);
}
} // namespace

void Display::render()
{
    std::lock_guard<std::mutex> lock(renderMutex);
    if (shuttingDown || !engine || engine->logicalDevice == VK_NULL_HANDLE)
    {
        return;
    }

    const int MAX_FRAMES_IN_FLIGHT = 1;

    if (framebufferResized)
    {
        framebufferResized = false;
        recreateSwapchain();
    }

    if (imageAvailableSemaphores.empty())
    {
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, {}, VK_FENCE_CREATE_SIGNALED_BIT};

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (vkCreateSemaphore(engine->logicalDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(engine->logicalDevice, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create sync objects for frame " + std::to_string(i));
            }
        }
    }

    const size_t swapchainImageCount = swapchainFramebuffers.size();
    if (renderFinishedSemaphores.size() != swapchainImageCount)
    {
        for (auto& sem : renderFinishedSemaphores)
        {
            if (sem != VK_NULL_HANDLE)
            {
                vkDestroySemaphore(engine->logicalDevice, sem, nullptr);
                sem = VK_NULL_HANDLE;
            }
        }
        renderFinishedSemaphores.assign(swapchainImageCount, VK_NULL_HANDLE);

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (size_t i = 0; i < swapchainImageCount; ++i)
        {
            if (vkCreateSemaphore(engine->logicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create per-image render finished semaphore " + std::to_string(i));
            }
        }
    }

    if (imagesInFlight.size() != swapchainImageCount)
    {
        imagesInFlight.assign(swapchainImageCount, VK_NULL_HANDLE);
    }

    updateOverlayBitmap(currentFps);

    if (firstFrame)
    {
        firstFrame = false;
    }
    else
    {
        vkWaitForFences(engine->logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    }

    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(
        engine->logicalDevice,
        swapchain,
        UINT64_MAX,
        imageAvailableSemaphores[currentFrame],
        VK_NULL_HANDLE,
        &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to acquire swapchain image.");
    }

    if (imageIndex >= imagesInFlight.size())
    {
        recreateSwapchain();
        return;
    }

    if (imagesInFlight[imageIndex] != VK_NULL_HANDLE)
    {
        vkWaitForFences(engine->logicalDevice, 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX);
    }
    imagesInFlight[imageIndex] = inFlightFences[currentFrame];

    for (auto& camera : cameras)
    {
        camera->update(imageIndex);
    }

    vkResetFences(engine->logicalDevice, 1, &inFlightFences[currentFrame]);

    if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to reset command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording command buffer");
    }

    std::array<VkClearValue, 3> clearValues = {};
    clearValues[0].color = {{bgColorR, bgColorG, bgColorB, 1.0f}};
    clearValues[1].depthStencil = {1.0f, 0};
    clearValues[2].color = {{0.0f, 0.0f, 0.0f, 0.0f}};

    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea = {{0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    for (size_t cameraIndex = 0; cameraIndex < cameras.size(); cameraIndex++)
    {
        auto& camera = cameras[cameraIndex];

        VkViewport viewport{};
        viewport.x = camera->centerpoint.x - camera->width / 2.0f;
        viewport.y = camera->centerpoint.y - camera->height / 2.0f;
        viewport.width = camera->width;
        viewport.height = camera->height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = {static_cast<int32_t>(viewport.x), static_cast<int32_t>(viewport.y)};
        scissor.extent = {static_cast<uint32_t>(viewport.width), static_cast<uint32_t>(viewport.height)};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        vkCmdBindDescriptorSets(
            commandBuffer,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipelineLayout,
            0,
            1,
            &camera->descriptorSet,
            0,
            nullptr);

        Frustum frustum = extractFrustum(camera->getProjectionMatrix() * camera->getViewMatrix());

        auto drawPass = [&](bool transparentPass)
        {
            for (const auto& modelPtr : engine->models)
            {
                if (!modelPtr || !modelPtr->visible)
                {
                    continue;
                }
                if (!embeddedMode && !cullingDisabled &&
                    !sphereInFrustum(frustum, modelPtr->boundsCenter, modelPtr->boundsRadius))
                {
                    continue;
                }

                const Model& model = *modelPtr;
                for (const auto& mesh : model.meshes)
                {
                    for (const auto& primitive : mesh.primitives)
                    {
                        const bool isTransparent = primitive->alphaMode == PrimitiveAlphaMode::Blend;
                        if (isTransparent != transparentPass)
                        {
                            continue;
                        }

                        const uint32_t pipelineIndex = pipelineIndexForCullMode(primitive->cullMode, cullingDisabled, use2DPipeline);
                        const bool useSkinnedPipeline = primitive->gpuSkinningEnabled && !use2DPipeline;
                        const VkPipeline activePipeline = useSkinnedPipeline
                            ? (transparentPass ? transparentSkinnedGraphicsPipelines[pipelineIndex]
                                               : skinnedGraphicsPipelines[pipelineIndex])
                            : (transparentPass ? transparentGraphicsPipelines[pipelineIndex]
                                               : graphicsPipelines[pipelineIndex]);

                        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, activePipeline);

                        if (primitive->vertexCount == 0)
                        {
                            std::cerr << "[Warning] Skipping primitive in model " << model.name << " due to zero vertices." << std::endl;
                            continue;
                        }
                        if (primitive->ObjectTransformUBOBufferMemory == VK_NULL_HANDLE)
                        {
                            std::cerr << "[Warning] Skipping primitive in model " << model.name << " due to missing transform buffer memory." << std::endl;
                            continue;
                        }
                        if (primitive->ObjectTransformUBOMapped == nullptr)
                        {
                            std::cerr << "[Warning] Skipping primitive in model " << model.name << " due to unmapped transform buffer." << std::endl;
                            continue;
                        }

                        primitive->uploadInstanceTransforms();
                        ObjectTransform perObjectTransformUBO = primitive->buildObjectTransformData();
                        uint32_t activeInstanceCount = static_cast<uint32_t>(perObjectTransformUBO.instanceData.x);
                        std::memcpy(primitive->ObjectTransformUBOMapped, &perObjectTransformUBO, sizeof(perObjectTransformUBO));

                        if (primitive->primitiveDescriptorSet != VK_NULL_HANDLE)
                        {
                            vkCmdBindDescriptorSets(
                                commandBuffer,
                                VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout,
                                1,
                                1,
                                &primitive->primitiveDescriptorSet,
                                0,
                                nullptr);
                        }
                        else
                        {
                            throw std::runtime_error("Primitive descriptor set is null");
                        }

                        VkBuffer vertexBuffers[] = {primitive->vertexBuffer};
                        VkDeviceSize offsets[] = {0};
                        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

                        if (primitive->indexCount > 0 && primitive->indexBuffer != VK_NULL_HANDLE)
                        {
                            vkCmdBindIndexBuffer(commandBuffer, primitive->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                            vkCmdDrawIndexed(commandBuffer, primitive->indexCount, activeInstanceCount, 0, 0, 0);
                        }
                        else
                        {
                            vkCmdDraw(commandBuffer, primitive->vertexCount, activeInstanceCount, 0, 0);
                        }
                    }
                }
            }
        };

        drawPass(false);
        drawPass(true);
    }

    vkCmdEndRenderPass(commandBuffer);

    recordOverlayCopy(commandBuffer, imageIndex);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record command buffer");
    }

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore renderFinishedSemaphore = renderFinishedSemaphores[imageIndex];
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(graphicsQueue, &presentInfo);

    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapchain();
    }
    else if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image");
    }

    fpsFrameCounter++;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::duration<double>>(now - fpsLastSampleTime);
    if (elapsed.count() >= 0.5)
    {
        double seconds = elapsed.count();
        currentFps = seconds > 0.0 ? static_cast<float>(fpsFrameCounter / seconds) : 0.0f;
        fpsFrameCounter = 0;
        fpsLastSampleTime = now;
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    glfwPollEvents();
}
