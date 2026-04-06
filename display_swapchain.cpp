#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

#include "display.h"
#include "engine.h"

void Display::createSwapchain()
{
    // Set MSAA samples from engine
    swapchainManager.setMsaaSamples(engine->getMsaaSampleCount());
    
    // Initialize swapchain manager
    swapchainManager.initialize(engine, surface, width, height);
}

void Display::cleanupSwapchainResources()
{
    swapchainManager.cleanupSwapchainResources();
}

void Display::recreateSwapchain()
{
    if (!engine || engine->logicalDevice == VK_NULL_HANDLE)
    {
        return;
    }

    int fbWidth = width;
    int fbHeight = height;
    while (fbWidth == 0 || fbHeight == 0)
    {
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        glfwWaitEvents();
    }
    width = fbWidth;
    height = fbHeight;

    vkDeviceWaitIdle(engine->logicalDevice);
    
    swapchainManager.recreateSwapchain(width, height);
    
    currentFrame = 0;
    firstFrame = true;
    updateCameraViewports();
}
