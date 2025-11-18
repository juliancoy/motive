#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan.h>
#include <algorithm>
#include <iostream>
#include "display.h"
#include "engine.h"
#include "model.h"
#include "camera.h"

void Display::createCommandPool()
{
    // Use the command pool from Engine class
    commandPool = engine->commandPool;

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(engine->logicalDevice, &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    // Verify command buffer is valid
    if (commandBuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("Command buffer is null after allocation");
    }
}


Display::Display(Engine* engine, int width, int height, const char* title){
    this->engine = engine;
    this->width = width;
    this->height = height;
    
    graphicsQueue = engine->graphicsQueue;
    commandPool = VK_NULL_HANDLE;
    vertShaderModule = VK_NULL_HANDLE;
    fragShaderModule = VK_NULL_HANDLE;

    // Create window and surface
    createWindow(title);

    // Create command buffer using engine's command pool
    createCommandPool();
    if (!commandBuffer)
    {
        throw std::runtime_error("Command buffer allocation failed");
    }

    std::cout << "About to create swapchain" << std::endl;
    // Ensure all operations are complete before proceeding
    vkDeviceWaitIdle(engine->logicalDevice);

    // Initialize swapchain before main loop
    createSwapchain();
    if (!swapchain) {
        throw std::runtime_error("Swapchain creation failed");
    }

    // Create graphics pipeline now that swapchain and render pass exist
    createGraphicsPipeline();
    if (!graphicsPipeline) {
        throw std::runtime_error("Graphics pipeline creation failed");
    }

}

void Display::addCamera(Camera* camera)
{
    if (!camera)
    {
        return;
    }
    camera->setWindow(window);
    cameras.push_back(camera);
    if (graphicsPipeline != VK_NULL_HANDLE)
    {
        camera->allocateDescriptorSet();
    }
}



void Display::createSwapchain() {
    // Wait for any pending operations to complete
    vkDeviceWaitIdle(engine->logicalDevice);
    int graphicsFamilyIndex = UINT32_MAX;
    // Find the graphics queue family index if not already set
    if (graphicsFamilyIndex == UINT32_MAX) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(engine->physicalDevice, i, surface, &presentSupport);
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
                graphicsFamilyIndex = i;
                break;
            }
        }

        if (graphicsFamilyIndex == UINT32_MAX) {
            throw std::runtime_error("Failed to find a suitable graphics queue family!");
        }
    }

    // Create the dedicated command pool for swapchain operations
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |  // Short-lived command buffers
                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;  // Allow individual resets
    
    if (vkCreateCommandPool(engine->logicalDevice, &poolInfo, nullptr, &swapchainCmdPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain command pool!");
    }

    // Allocate the command buffers (both regular and recreation ones)
    VkCommandBufferAllocateInfo swapallocInfo{};
    swapallocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    swapallocInfo.commandPool = swapchainCmdPool;
    swapallocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    swapallocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(engine->logicalDevice, &swapallocInfo, &swapchainRecreationCmdBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate swapchain command buffer!");
    }

    // Create the fence for swapchain recreation synchronization
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start in signaled state
    
    if (vkCreateFence(engine->logicalDevice, &fenceInfo, nullptr, &swapchainRecreationFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain recreation fence!");
    }

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(engine->physicalDevice, surface, &capabilities) != VK_SUCCESS) {
        throw std::runtime_error("Failed to get surface capabilities");
    }

    std::cout << "Surface Capabilities:\n";
    std::cout << "  minImageCount: " << capabilities.minImageCount << "\n";
    std::cout << "  maxImageCount: " << capabilities.maxImageCount << "\n";
    std::cout << "  currentExtent: " << capabilities.currentExtent.width << "x" << capabilities.currentExtent.height << "\n";

    // Query surface formats
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, surface, &formatCount, formats.data());

    // Pick the first available format as default
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& availableFormat : formats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = availableFormat;
            break;
        }
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<uint32_t>::max()) {
        extent.width = std::max(capabilities.minImageExtent.width, std::min(extent.width, capabilities.maxImageExtent.width));
        extent.height = std::max(capabilities.minImageExtent.height, std::min(extent.height, capabilities.maxImageExtent.height));
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    // Create swapchain
    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(engine->logicalDevice, &swapchainInfo, nullptr, &swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain!");
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(engine->logicalDevice, swapchain, &imageCount, nullptr);
    std::vector<VkImage> swapchainImages(imageCount);
    vkGetSwapchainImagesKHR(engine->logicalDevice, swapchain, &imageCount, swapchainImages.data());

    // Create image views
    swapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(engine->logicalDevice, &viewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image views!");
        }
    }

    // Create render pass
    std::array<VkAttachmentDescription, 2> attachments = {};
    // Color attachment
    attachments[0].format = surfaceFormat.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(engine->logicalDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass!");
    }

    // Create depth resources
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent = {extent.width, extent.height, 1};
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = depthFormat;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(engine->logicalDevice, &depthImageInfo, nullptr, &depthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(engine->logicalDevice, depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &depthImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate depth image memory!");
    }

    vkBindImageMemory(engine->logicalDevice, depthImage, depthImageMemory, 0);

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = depthFormat;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(engine->logicalDevice, &depthViewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view!");
    }

    // Ensure swapchain recreation objects are valid
    if (swapchainRecreationFence == VK_NULL_HANDLE || swapchainRecreationCmdBuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("Swapchain recreation objects not initialized");
    }

    // Wait for previous swapchain recreation to complete
    VkResult fenceWait = vkWaitForFences(engine->logicalDevice, 1, &swapchainRecreationFence, VK_TRUE, UINT64_MAX);
    if (fenceWait != VK_SUCCESS) {
        throw std::runtime_error("Failed to wait for swapchain recreation fence");
    }
    if (vkResetFences(engine->logicalDevice, 1, &swapchainRecreationFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to reset swapchain recreation fence");
    }

    // Reset and begin the command buffer
    if (vkResetCommandBuffer(swapchainRecreationCmdBuffer, 0) != VK_SUCCESS) {
        throw std::runtime_error("Failed to reset swapchain recreation command buffer");
    }
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if (vkBeginCommandBuffer(swapchainRecreationCmdBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin command buffer for depth image layout transition");
    }

    // Transition depth image layout
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = depthImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | 
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(
        swapchainRecreationCmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(swapchainRecreationCmdBuffer);

    // Submit the command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &swapchainRecreationCmdBuffer;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, swapchainRecreationFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit depth image layout transition commands");
    }

    // Create framebuffers
    swapchainFramebuffers.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews[i],
            depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(engine->logicalDevice, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }
}


void Display::createWindow(const char *title)
{
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        throw std::runtime_error("Failed to create GLFW window");
    }

    // Set window user pointer and callbacks
    glfwSetWindowUserPointer(window, this);

    createSurface(window);
    if (!surface)
    {
        throw std::runtime_error("Surface creation failed");
    }

    // --------- GET THE QUEUE FAMILY INDEX ----------
    // Queue families
    // Check queue family support
    uint32_t queueFamilyCount = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, queueFamilies.data());

    bool foundSuitableQueue = false;
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(engine->physicalDevice, i, surface, &presentSupport);

        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
        {
            graphicsQueueFamilyIndex = i;
            foundSuitableQueue = true;
            break;
        }
    }

    if (!foundSuitableQueue)
    {
        throw std::runtime_error("Failed to find queue family with graphics and present support");
    }

    // Verify swapchain support
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(engine->physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(engine->physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    for (const auto &extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    if (!requiredExtensions.empty())
    {
        throw std::runtime_error("Missing required device extensions");
    }
}


void Display::createSurface(GLFWwindow *window)
{
    if (!engine->instance)
    {
        throw std::runtime_error("Vulkan engine->instance not initialized");
    }
    if (!window)
    {
        throw std::runtime_error("Invalid GLFW window");
    }

    VkResult result = glfwCreateWindowSurface(engine->instance, window, nullptr, &surface);
    if (result != VK_SUCCESS || surface == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Failed to create window surface: " + std::to_string(result));
    }
}


void Display::render()
{
    const int MAX_FRAMES_IN_FLIGHT = 2;
    if (imageAvailableSemaphores.empty())
    {
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, {}, VK_FENCE_CREATE_SIGNALED_BIT};

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (vkCreateSemaphore(engine->logicalDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(engine->logicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(engine->logicalDevice, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create sync objects for frame " + std::to_string(i));
            }
        }
    }

    static size_t currentFrame = 0;

    if (firstFrame)
        firstFrame = false;
    else
    {
        vkWaitForFences(engine->logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(engine->logicalDevice, swapchain, UINT64_MAX,
                                            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        createSwapchain(); // optional
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to acquire swapchain image.");
    }

    // Update all cameras with current transformation matrices
    for (auto& camera : cameras) {
        camera->update(imageIndex);
    }

    vkResetFences(engine->logicalDevice, 1, &inFlightFences[currentFrame]);

    // Ensure command buffer is not in use before resetting
    vkDeviceWaitIdle(engine->logicalDevice);

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

    std::array<VkClearValue, 2> clearValues = {};
    clearValues[0].color = {{0.2f, 0.2f, 0.8f, 1.0f}}; // Color clear (blue)
    clearValues[1].depthStencil = {1.0f, 0}; // Depth clear

    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea = {{0, 0}, {static_cast<uint32_t>(width), static_cast<uint32_t>(height)}};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

    // Render for each camera
    for (size_t cameraIndex = 0; cameraIndex < cameras.size(); cameraIndex++) {
        auto& camera = cameras[cameraIndex];
        
        // Set viewport for this camera
        VkViewport viewport{};
        viewport.x = camera->centerpoint.x - camera->width / 2.0f;
        viewport.y = camera->centerpoint.y - camera->height / 2.0f;
        viewport.width = camera->width;
        viewport.height = camera->height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        // Set scissor for this camera
        VkRect2D scissor{};
        scissor.offset = {static_cast<int32_t>(viewport.x), static_cast<int32_t>(viewport.y)};
        scissor.extent = {static_cast<uint32_t>(viewport.width), static_cast<uint32_t>(viewport.height)};
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // Bind this camera's UBO descriptor set (set 0)
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipelineLayout, 0, 1, &camera->descriptorSet, 0, nullptr);

        for (const auto& modelPtr : engine->models)
        {
            if (!modelPtr)
            {
                continue;
            }
            const Model& model = *modelPtr;
            for (const auto& mesh : model.meshes)
            {
                for (const auto& primitive : mesh.primitives) {  // This is now unique_ptr<Primitive>
                    if (primitive->vertexCount == 0) {
                        std::cerr << "[Warning] Skipping primitive in model " << model.name << " due to zero vertices." << std::endl;
                        continue;
                    }
                    if (primitive->ObjectTransformUBOBufferMemory == VK_NULL_HANDLE) {
                        std::cerr << "[Warning] Skipping primitive in model " << model.name << " due to missing transform buffer memory." << std::endl;
                        continue;
                    }
                    if (primitive->ObjectTransformUBOMapped == nullptr) {
                        std::cerr << "[Warning] Skipping primitive in model " << model.name << " due to unmapped transform buffer." << std::endl;
                        continue;
                    }
                    // Update UBO with object's transform
                    ObjectTransform perObjectTransformUBO{};
                    perObjectTransformUBO.model = primitive->transform;

                    memcpy(primitive->ObjectTransformUBOMapped, &perObjectTransformUBO, sizeof(perObjectTransformUBO));

                    // Bind primitive's texture descriptor set (set 1)
                    if (primitive->primitiveDescriptorSet != VK_NULL_HANDLE) {
                        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                              pipelineLayout, 1, 1, &primitive->primitiveDescriptorSet, 0, nullptr);
                    } else {
                        throw std::runtime_error("Primitive descriptor set is null");
                    }

                    VkBuffer vertexBuffers[] = {primitive->vertexBuffer};
                    VkDeviceSize offsets[] = {0};
                    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

                    if (primitive->indexCount > 0 && primitive->indexBuffer != VK_NULL_HANDLE) {
                        vkCmdBindIndexBuffer(commandBuffer, primitive->indexBuffer, 0, VK_INDEX_TYPE_UINT32);
                        vkCmdDrawIndexed(commandBuffer, primitive->indexCount, 1, 0, 0, 0);
                    } else {
                        vkCmdDraw(commandBuffer, primitive->vertexCount, 1, 0, 0);
                    }
                }
            }
        }
    }

    vkCmdEndRenderPass(commandBuffer);

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

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
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
    
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    glfwPollEvents();
}


void Display::createGraphicsPipeline()
{
    // Load compiled SPIR-V shaders
    std::string vertPath = "shaders/mainforward.vert.spv";
    std::string fragPath = "shaders/mainforward.frag.spv";

    auto vertShaderCode = readSPIRVFile(vertPath);
    auto fragShaderCode = readSPIRVFile(fragPath);

    // Create shader modules
    vertShaderModule = engine->createShaderModule(vertShaderCode);
    fragShaderModule = engine->createShaderModule(fragShaderCode);

    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Dynamic state
    std::vector<VkDynamicState> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR
    };

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // Viewport and scissor (initial values, will be set dynamically)
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 800.0f;
    viewport.height = 600.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {800, 600};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    // rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // Ensure descriptor set layouts are initialized before creating the pipeline layout
    engine->createDescriptorSetLayouts();

    // Pipeline layout with single descriptor set
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    std::array<VkDescriptorSetLayout, 2> setLayouts = {engine->descriptorSetLayout, engine->primitiveDescriptorSetLayout};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(engine->logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout!");
    }
    engine->nameVulkanObject((uint64_t)pipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "displayPipelineLayout");


    // Graphics pipeline creation
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(engine->logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create graphics pipeline!");
    }

    // Allocate descriptor sets for all cameras now that layout is available
    for (auto& camera : cameras) {
        camera->allocateDescriptorSet();
    }
}

Display::~Display() {
    // Wait for device to be idle before cleanup
    if (engine && engine->logicalDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(engine->logicalDevice);
    }

    // Cleanup swapchain resources
    if (engine && engine->logicalDevice != VK_NULL_HANDLE) {
        // Destroy framebuffers
        for (auto framebuffer : swapchainFramebuffers) {
            if (framebuffer != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(engine->logicalDevice, framebuffer, nullptr);
                framebuffer = VK_NULL_HANDLE;
            }
        }

        // Destroy image views
        for (auto imageView : swapchainImageViews) {
            if (imageView != VK_NULL_HANDLE) {
                vkDestroyImageView(engine->logicalDevice, imageView, nullptr);
                imageView = VK_NULL_HANDLE;
            }
        }

        // Destroy swapchain
        if (swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(engine->logicalDevice, swapchain, nullptr);
            swapchain = VK_NULL_HANDLE;
        }

        // Destroy depth resources
        if (depthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(engine->logicalDevice, depthImageView, nullptr);
            depthImageView = VK_NULL_HANDLE;
        }
        if (depthImage != VK_NULL_HANDLE) {
            vkDestroyImage(engine->logicalDevice, depthImage, nullptr);
            depthImage = VK_NULL_HANDLE;
        }
        if (depthImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(engine->logicalDevice, depthImageMemory, nullptr);
            depthImageMemory = VK_NULL_HANDLE;
        }

        // Destroy pipeline
        if (graphicsPipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(engine->logicalDevice, graphicsPipeline, nullptr);
            graphicsPipeline = VK_NULL_HANDLE;
        }

        // Destroy shader modules
        if (vertShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(engine->logicalDevice, vertShaderModule, nullptr);
            vertShaderModule = VK_NULL_HANDLE;
        }
        if (fragShaderModule != VK_NULL_HANDLE) {
            vkDestroyShaderModule(engine->logicalDevice, fragShaderModule, nullptr);
            fragShaderModule = VK_NULL_HANDLE;
        }

        // Destroy render pass
        if (renderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(engine->logicalDevice, renderPass, nullptr);
            renderPass = VK_NULL_HANDLE;
        }

        // Destroy pipeline layout
        if (pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(engine->logicalDevice, pipelineLayout, nullptr);
            pipelineLayout = VK_NULL_HANDLE;
        }

        // Cleanup cameras (each camera manages its own UBO)
        for (auto& camera : cameras) {
            delete camera;
        }
        cameras.clear();

        // Free command buffers
        if (swapchainRecreationCmdBuffer != VK_NULL_HANDLE && swapchainCmdPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(engine->logicalDevice, swapchainCmdPool, 1, &swapchainRecreationCmdBuffer);
            swapchainRecreationCmdBuffer = VK_NULL_HANDLE;
        }
        if (commandBuffer != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(engine->logicalDevice, commandPool, 1, &commandBuffer);
            commandBuffer = VK_NULL_HANDLE;
        }

        // Destroy swapchain-specific command pool (engine command pool is owned by Engine)
        if (swapchainCmdPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(engine->logicalDevice, swapchainCmdPool, nullptr);
            swapchainCmdPool = VK_NULL_HANDLE;
        }

        // Destroy semaphores
        for (auto &sem : imageAvailableSemaphores) {
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(engine->logicalDevice, sem, nullptr);
                sem = VK_NULL_HANDLE;
            }
        }
        for (auto &sem : renderFinishedSemaphores) {
            if (sem != VK_NULL_HANDLE) {
                vkDestroySemaphore(engine->logicalDevice, sem, nullptr);
                sem = VK_NULL_HANDLE;
            }
        }

        // Destroy fences
        for (auto &fence : inFlightFences) {
            if (fence != VK_NULL_HANDLE) {
                vkDestroyFence(engine->logicalDevice, fence, nullptr);
                fence = VK_NULL_HANDLE;
            }
        }
        if (swapchainRecreationFence != VK_NULL_HANDLE) {
            vkDestroyFence(engine->logicalDevice, swapchainRecreationFence, nullptr);
            swapchainRecreationFence = VK_NULL_HANDLE;
        }
    }

    // Destroy surface
    if (engine && engine->instance != VK_NULL_HANDLE && surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(engine->instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }

    // Destroy GLFW window
    if (window != nullptr) {
        glfwDestroyWindow(window);
        window = nullptr;
    }

    // Terminate GLFW
    glfwTerminate();
}
