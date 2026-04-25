#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "input_router.h"
#include "primitive.h"

void Display::createCommandPool()
{
    commandPool = engine->commandPool;

    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(engine->logicalDevice, &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    if (commandBuffer == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Command buffer is null after allocation");
    }
}

Display::Display(Engine* engine, int width, int height, const char* title, bool disableCulling, bool use2DPipeline, bool embeddedMode)
{
    this->engine = engine;
    this->width = width;
    this->height = height;
    this->cullingDisabled = disableCulling;
    this->use2DPipeline = use2DPipeline;
    this->embeddedMode = embeddedMode;
    fpsLastSampleTime = std::chrono::steady_clock::now();
    currentFps = 0.0f;
    fpsFrameCounter = 0;

    inputRouter = std::make_unique<InputRouter>();
    inputRouter->setDisplay(this);

    graphicsQueue = engine->graphicsQueue;
    graphicsPipelines.fill(VK_NULL_HANDLE);
    transparentGraphicsPipelines.fill(VK_NULL_HANDLE);
    skinnedGraphicsPipelines.fill(VK_NULL_HANDLE);
    transparentSkinnedGraphicsPipelines.fill(VK_NULL_HANDLE);

    createWindow(title);
    createCommandPool();
    if (commandBuffer == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Command buffer allocation failed");
    }

    std::cout << "About to create swapchain" << std::endl;
    vkDeviceWaitIdle(engine->logicalDevice);

    createSwapchain();
    if (swapchainManager.getSwapchain() == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Swapchain creation failed");
    }

    createGraphicsPipeline();
    if (graphicsPipelines[static_cast<size_t>(PrimitiveCullMode::Back)] == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Graphics pipeline creation failed");
    }
}

void Display::setBackgroundColor(float r, float g, float b)
{
    bgColorR = r;
    bgColorG = g;
    bgColorB = b;
}

void Display::shutdown()
{
    std::lock_guard<std::mutex> lock(renderMutex);
    if (shuttingDown)
    {
        return;
    }
    shuttingDown = true;
    if (engine && engine->logicalDevice != VK_NULL_HANDLE)
    {
        vkQueueWaitIdle(graphicsQueue);
        vkDeviceWaitIdle(engine->logicalDevice);
    }
    frameSyncState.reset();
}

Display::~Display()
{
    shutdown();

    if (window != nullptr)
    {
        glfwSetFramebufferSizeCallback(window, nullptr);
        glfwSetMouseButtonCallback(window, nullptr);
        glfwSetCursorPosCallback(window, nullptr);
        glfwSetKeyCallback(window, nullptr);
        glfwSetWindowUserPointer(window, nullptr);
    }

    destroyOverlayBuffer();

    if (engine && engine->logicalDevice != VK_NULL_HANDLE)
    {
        swapchainManager.shutdown();

        for (VkPipeline& pipeline : graphicsPipelines)
        {
            if (pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(engine->logicalDevice, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
        for (VkPipeline& pipeline : transparentGraphicsPipelines)
        {
            if (pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(engine->logicalDevice, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
        for (VkPipeline& pipeline : skinnedGraphicsPipelines)
        {
            if (pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(engine->logicalDevice, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }
        for (VkPipeline& pipeline : transparentSkinnedGraphicsPipelines)
        {
            if (pipeline != VK_NULL_HANDLE)
            {
                vkDestroyPipeline(engine->logicalDevice, pipeline, nullptr);
                pipeline = VK_NULL_HANDLE;
            }
        }

        if (vertShaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(engine->logicalDevice, vertShaderModule, nullptr);
            vertShaderModule = VK_NULL_HANDLE;
        }
        if (skinnedVertShaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(engine->logicalDevice, skinnedVertShaderModule, nullptr);
            skinnedVertShaderModule = VK_NULL_HANDLE;
        }
        if (fragShaderModule != VK_NULL_HANDLE)
        {
            vkDestroyShaderModule(engine->logicalDevice, fragShaderModule, nullptr);
            fragShaderModule = VK_NULL_HANDLE;
        }

        if (pipelineLayout != VK_NULL_HANDLE)
        {
            vkDestroyPipelineLayout(engine->logicalDevice, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        cameras.clear();
        ownedCameras.clear();

        if (commandBuffer != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE)
        {
            vkFreeCommandBuffers(engine->logicalDevice, commandPool, 1, &commandBuffer);
            commandBuffer = VK_NULL_HANDLE;
        }
    }

    if (engine && engine->instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(engine->instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    if (window != nullptr)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    glfwTerminate();
}
