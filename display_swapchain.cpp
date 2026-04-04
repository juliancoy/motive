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
    vkDeviceWaitIdle(engine->logicalDevice);

    uint32_t graphicsFamilyIndex = UINT32_MAX;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(engine->physicalDevice, i, surface, &presentSupport);
        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
        {
            graphicsFamilyIndex = i;
            break;
        }
    }

    if (graphicsFamilyIndex == UINT32_MAX)
    {
        throw std::runtime_error("Failed to find a suitable graphics queue family!");
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(engine->logicalDevice, &poolInfo, nullptr, &swapchainCmdPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain command pool!");
    }

    VkCommandBufferAllocateInfo swapallocInfo{};
    swapallocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    swapallocInfo.commandPool = swapchainCmdPool;
    swapallocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    swapallocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(engine->logicalDevice, &swapallocInfo, &swapchainRecreationCmdBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate swapchain command buffer!");
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateFence(engine->logicalDevice, &fenceInfo, nullptr, &swapchainRecreationFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain recreation fence!");
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(engine->physicalDevice, surface, &capabilities) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to get surface capabilities");
    }

    std::cout << "Surface Capabilities:\n";
    std::cout << "  minImageCount: " << capabilities.minImageCount << "\n";
    std::cout << "  maxImageCount: " << capabilities.maxImageCount << "\n";
    std::cout << "  currentExtent: " << capabilities.currentExtent.width << "x" << capabilities.currentExtent.height << "\n";

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, surface, &formatCount, formats.data());

    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& availableFormat : formats)
    {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFormat = availableFormat;
            break;
        }
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<uint32_t>::max())
    {
        extent.width = std::max(capabilities.minImageExtent.width, std::min(extent.width, capabilities.maxImageExtent.width));
        extent.height = std::max(capabilities.minImageExtent.height, std::min(extent.height, capabilities.maxImageExtent.height));
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(engine->logicalDevice, &swapchainInfo, nullptr, &swapchain) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain!");
    }

    vkGetSwapchainImagesKHR(engine->logicalDevice, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(engine->logicalDevice, swapchain, &imageCount, swapchainImages.data());

    swapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++)
    {
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

        if (vkCreateImageView(engine->logicalDevice, &viewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create image views!");
        }
    }

    std::array<VkAttachmentDescription, 3> attachments = {};
    attachments[0].format = surfaceFormat.format;
    attachments[0].samples = msaaSamples;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = msaaSamples;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    attachments[2].format = surfaceFormat.format;
    attachments[2].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[2].loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[2].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[2].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[2].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[2].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef{};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference resolveAttachmentRef{};
    resolveAttachmentRef.attachment = 2;
    resolveAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;
    subpass.pResolveAttachments = &resolveAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(engine->logicalDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create render pass!");
    }

    VkImageCreateInfo colorImageInfo{};
    colorImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    colorImageInfo.imageType = VK_IMAGE_TYPE_2D;
    colorImageInfo.extent = {extent.width, extent.height, 1};
    colorImageInfo.mipLevels = 1;
    colorImageInfo.arrayLayers = 1;
    colorImageInfo.format = surfaceFormat.format;
    colorImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    colorImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorImageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    colorImageInfo.samples = msaaSamples;
    colorImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(engine->logicalDevice, &colorImageInfo, nullptr, &colorImage) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create MSAA color image!");
    }

    VkMemoryRequirements colorMemRequirements;
    vkGetImageMemoryRequirements(engine->logicalDevice, colorImage, &colorMemRequirements);

    VkMemoryAllocateInfo colorAllocInfo{};
    colorAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    colorAllocInfo.allocationSize = colorMemRequirements.size;
    colorAllocInfo.memoryTypeIndex = engine->findMemoryType(colorMemRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(engine->logicalDevice, &colorAllocInfo, nullptr, &colorImageMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate MSAA color image memory!");
    }
    vkBindImageMemory(engine->logicalDevice, colorImage, colorImageMemory, 0);

    VkImageViewCreateInfo colorViewInfo{};
    colorViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    colorViewInfo.image = colorImage;
    colorViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    colorViewInfo.format = surfaceFormat.format;
    colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    colorViewInfo.subresourceRange.baseMipLevel = 0;
    colorViewInfo.subresourceRange.levelCount = 1;
    colorViewInfo.subresourceRange.baseArrayLayer = 0;
    colorViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(engine->logicalDevice, &colorViewInfo, nullptr, &colorImageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create MSAA color image view!");
    }

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
    depthImageInfo.samples = msaaSamples;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(engine->logicalDevice, &depthImageInfo, nullptr, &depthImage) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(engine->logicalDevice, depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &depthImageMemory) != VK_SUCCESS)
    {
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

    if (vkCreateImageView(engine->logicalDevice, &depthViewInfo, nullptr, &depthImageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image view!");
    }

    if (swapchainRecreationFence == VK_NULL_HANDLE || swapchainRecreationCmdBuffer == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Swapchain recreation objects not initialized");
    }

    VkResult fenceWait = vkWaitForFences(engine->logicalDevice, 1, &swapchainRecreationFence, VK_TRUE, UINT64_MAX);
    if (fenceWait != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to wait for swapchain recreation fence");
    }
    if (vkResetFences(engine->logicalDevice, 1, &swapchainRecreationFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to reset swapchain recreation fence");
    }

    if (vkResetCommandBuffer(swapchainRecreationCmdBuffer, 0) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to reset swapchain recreation command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(swapchainRecreationCmdBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin command buffer for attachment layout transitions");
    }

    std::array<VkImageMemoryBarrier, 2> barriers{};

    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = colorImage;
    barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barriers[0].subresourceRange.baseMipLevel = 0;
    barriers[0].subresourceRange.levelCount = 1;
    barriers[0].subresourceRange.baseArrayLayer = 0;
    barriers[0].subresourceRange.layerCount = 1;
    barriers[0].srcAccessMask = 0;
    barriers[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[1].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[1].image = depthImage;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 1;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(
        swapchainRecreationCmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(barriers.size()),
        barriers.data());

    vkEndCommandBuffer(swapchainRecreationCmdBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &swapchainRecreationCmdBuffer;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, swapchainRecreationFence) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit attachment layout transition commands");
    }

    swapchainFramebuffers.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++)
    {
        std::array<VkImageView, 3> framebufferAttachments = {colorImageView, depthImageView, swapchainImageViews[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(framebufferAttachments.size());
        framebufferInfo.pAttachments = framebufferAttachments.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(engine->logicalDevice, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }
}

void Display::cleanupSwapchainResources()
{
    if (!engine || engine->logicalDevice == VK_NULL_HANDLE)
    {
        return;
    }

    vkQueueWaitIdle(graphicsQueue);
    vkDeviceWaitIdle(engine->logicalDevice);

    for (auto framebuffer : swapchainFramebuffers)
    {
        if (framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(engine->logicalDevice, framebuffer, nullptr);
        }
    }
    swapchainFramebuffers.clear();

    for (auto imageView : swapchainImageViews)
    {
        if (imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(engine->logicalDevice, imageView, nullptr);
        }
    }
    swapchainImageViews.clear();

    if (colorImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, colorImageView, nullptr);
        colorImageView = VK_NULL_HANDLE;
    }
    if (colorImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, colorImage, nullptr);
        colorImage = VK_NULL_HANDLE;
    }
    if (colorImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, colorImageMemory, nullptr);
        colorImageMemory = VK_NULL_HANDLE;
    }

    if (depthImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, depthImageView, nullptr);
        depthImageView = VK_NULL_HANDLE;
    }
    if (depthImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, depthImage, nullptr);
        depthImage = VK_NULL_HANDLE;
    }
    if (depthImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, depthImageMemory, nullptr);
        depthImageMemory = VK_NULL_HANDLE;
    }

    if (renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(engine->logicalDevice, renderPass, nullptr);
        renderPass = VK_NULL_HANDLE;
    }

    if (swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(engine->logicalDevice, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    swapchainImages.clear();

    if (swapchainRecreationCmdBuffer != VK_NULL_HANDLE && swapchainCmdPool != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(engine->logicalDevice, swapchainCmdPool, 1, &swapchainRecreationCmdBuffer);
        swapchainRecreationCmdBuffer = VK_NULL_HANDLE;
    }

    if (swapchainCmdPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(engine->logicalDevice, swapchainCmdPool, nullptr);
        swapchainCmdPool = VK_NULL_HANDLE;
    }

    if (swapchainRecreationFence != VK_NULL_HANDLE)
    {
        vkDestroyFence(engine->logicalDevice, swapchainRecreationFence, nullptr);
        swapchainRecreationFence = VK_NULL_HANDLE;
    }

    for (auto& sem : renderFinishedSemaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(engine->logicalDevice, sem, nullptr);
            sem = VK_NULL_HANDLE;
        }
    }
    renderFinishedSemaphores.clear();

    for (auto& sem : imageAvailableSemaphores)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(engine->logicalDevice, sem, nullptr);
            sem = VK_NULL_HANDLE;
        }
    }
    imageAvailableSemaphores.clear();

    for (auto& fence : inFlightFences)
    {
        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(engine->logicalDevice, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
    }
    inFlightFences.clear();

    imagesInFlight.clear();
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
    cleanupSwapchainResources();
    createSwapchain();
    currentFrame = 0;
    firstFrame = true;
    updateCameraViewports();
}
