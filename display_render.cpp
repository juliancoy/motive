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

bool cameraUsesCustomViewport(const Camera& camera)
{
    constexpr float kViewportEpsilon = 0.001f;
    if (!camera.isFullscreenViewportEnabled())
    {
        return true;
    }

    return std::abs(camera.getFullscreenPercentX() - 1.0f) > kViewportEpsilon ||
           std::abs(camera.getFullscreenPercentY() - 1.0f) > kViewportEpsilon;
}

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

    const uint32_t maxFramesInFlight = swapchainManager.getMaxFramesInFlight();

    if (framebufferResized)
    {
        framebufferResized = false;
        recreateSwapchain();
    }

    const size_t swapchainImageCount = swapchainManager.getFramebufferCount();

    updateOverlayBitmap(currentFps);

    if (firstFrame)
    {
        firstFrame = false;
    }
    else
    {
        VkFence fence = swapchainManager.getInFlightFence(currentFrame);
        vkWaitForFences(engine->logicalDevice, 1, &fence, VK_TRUE, UINT64_MAX);
    }

    uint32_t imageIndex = 0;
    VkResult result = static_cast<VkResult>(swapchainManager.acquireNextImage(currentFrame, &imageIndex));

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to acquire swapchain image.");
    }

    VkFence imageFence = swapchainManager.getImageInFlight(imageIndex);
    if (imageFence != VK_NULL_HANDLE)
    {
        vkWaitForFences(engine->logicalDevice, 1, &imageFence, VK_TRUE, UINT64_MAX);
    }
    swapchainManager.setImageInFlight(imageIndex, swapchainManager.getInFlightFence(currentFrame));

    std::vector<Camera*> renderCameras;
    renderCameras.reserve(cameras.size());

    const bool hasCustomViewportLayout = std::any_of(cameras.begin(), cameras.end(),
        [](Camera* camera)
        {
            return camera && cameraUsesCustomViewport(*camera);
        });

    if (hasCustomViewportLayout)
    {
        for (Camera* camera : cameras)
        {
        if (camera && camera->width > 1.0f && camera->height > 1.0f)
        {
            renderCameras.push_back(camera);
        }
        }
    }
    else if (Camera* activeCamera = getActiveCamera())
    {
        renderCameras.push_back(activeCamera);
    }

    for (Camera* camera : renderCameras)
    {
        camera->update(imageIndex);
    }

    VkFence inFlightFence = swapchainManager.getInFlightFence(currentFrame);
    vkResetFences(engine->logicalDevice, 1, &inFlightFence);

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
    renderPassInfo.renderPass = swapchainManager.getRenderPass();
    renderPassInfo.framebuffer = swapchainManager.getFramebuffer(imageIndex);
    renderPassInfo.renderArea = {{0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    for (size_t cameraIndex = 0; cameraIndex < renderCameras.size(); cameraIndex++)
    {
        Camera* camera = renderCameras[cameraIndex];
        if (!camera)
        {
            continue;
        }

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
    VkSemaphore waitSemaphores[] = {swapchainManager.getImageAvailableSemaphore(currentFrame)};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore renderFinishedSemaphore = swapchainManager.getRenderFinishedSemaphore(currentFrame);
    VkSemaphore signalSemaphores[] = {renderFinishedSemaphore};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    VkFence submitFence = swapchainManager.getInFlightFence(currentFrame);
    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, submitFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    result = swapchainManager.presentImage(imageIndex, currentFrame);

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

    currentFrame = (currentFrame + 1) % maxFramesInFlight;
    glfwPollEvents();
}
