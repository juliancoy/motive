#include "swapchain_manager.h"
#include "engine.h"
#include "vma_allocator.h"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

SwapchainManager::SwapchainManager() = default;

SwapchainManager::~SwapchainManager()
{
    shutdown();
}

SwapchainManager::SwapchainManager(SwapchainManager&& other) noexcept
    : engine_(other.engine_)
    , surface_(other.surface_)
    , swapchain_(other.swapchain_)
    , images_(std::move(other.images_))
    , imageViews_(std::move(other.imageViews_))
    , imageFormat_(other.imageFormat_)
    , extent_(other.extent_)
    , renderPass_(other.renderPass_)
    , framebuffers_(std::move(other.framebuffers_))
    , msaaColorImage_(other.msaaColorImage_)
    , msaaColorMemory_(other.msaaColorMemory_)
    , msaaColorView_(other.msaaColorView_)
    , depthImage_(other.depthImage_)
    , depthMemory_(other.depthMemory_)
    , depthView_(other.depthView_)
    , imageAvailableSemaphores_(std::move(other.imageAvailableSemaphores_))
    , renderFinishedSemaphores_(std::move(other.renderFinishedSemaphores_))
    , inFlightFences_(std::move(other.inFlightFences_))
    , imagesInFlight_(std::move(other.imagesInFlight_))
    , cmdPool_(other.cmdPool_)
    , cmdBuffer_(other.cmdBuffer_)
    , fence_(other.fence_)
    , msaaSamples_(other.msaaSamples_)
{
    other.engine_ = nullptr;
    other.surface_ = VK_NULL_HANDLE;
    other.swapchain_ = VK_NULL_HANDLE;
    other.renderPass_ = VK_NULL_HANDLE;
    other.msaaColorImage_ = VK_NULL_HANDLE;
    other.msaaColorMemory_ = VK_NULL_HANDLE;
    other.msaaColorView_ = VK_NULL_HANDLE;
    other.depthImage_ = VK_NULL_HANDLE;
    other.depthMemory_ = VK_NULL_HANDLE;
    other.depthView_ = VK_NULL_HANDLE;
    other.cmdPool_ = VK_NULL_HANDLE;
    other.cmdBuffer_ = VK_NULL_HANDLE;
    other.fence_ = VK_NULL_HANDLE;
}

SwapchainManager& SwapchainManager::operator=(SwapchainManager&& other) noexcept
{
    if (this != &other)
    {
        shutdown();
        
        engine_ = other.engine_;
        surface_ = other.surface_;
        swapchain_ = other.swapchain_;
        images_ = std::move(other.images_);
        imageViews_ = std::move(other.imageViews_);
        imageFormat_ = other.imageFormat_;
        extent_ = other.extent_;
        renderPass_ = other.renderPass_;
        framebuffers_ = std::move(other.framebuffers_);
        msaaColorImage_ = other.msaaColorImage_;
        msaaColorMemory_ = other.msaaColorMemory_;
        msaaColorView_ = other.msaaColorView_;
        depthImage_ = other.depthImage_;
        depthMemory_ = other.depthMemory_;
        depthView_ = other.depthView_;
        imageAvailableSemaphores_ = std::move(other.imageAvailableSemaphores_);
        renderFinishedSemaphores_ = std::move(other.renderFinishedSemaphores_);
        inFlightFences_ = std::move(other.inFlightFences_);
        imagesInFlight_ = std::move(other.imagesInFlight_);
        cmdPool_ = other.cmdPool_;
        cmdBuffer_ = other.cmdBuffer_;
        fence_ = other.fence_;
        msaaSamples_ = other.msaaSamples_;

        other.engine_ = nullptr;
        other.surface_ = VK_NULL_HANDLE;
        other.swapchain_ = VK_NULL_HANDLE;
        other.renderPass_ = VK_NULL_HANDLE;
        other.msaaColorImage_ = VK_NULL_HANDLE;
        other.msaaColorMemory_ = VK_NULL_HANDLE;
        other.msaaColorView_ = VK_NULL_HANDLE;
        other.depthImage_ = VK_NULL_HANDLE;
        other.depthMemory_ = VK_NULL_HANDLE;
        other.depthView_ = VK_NULL_HANDLE;
        other.cmdPool_ = VK_NULL_HANDLE;
        other.cmdBuffer_ = VK_NULL_HANDLE;
        other.fence_ = VK_NULL_HANDLE;
    }
    return *this;
}

void SwapchainManager::initialize(Engine* engine, VkSurfaceKHR surface, int width, int height)
{
    engine_ = engine;
    surface_ = surface;
    extent_.width = static_cast<uint32_t>(width);
    extent_.height = static_cast<uint32_t>(height);
    
    createSwapchain();
    createSyncObjects();
}

void SwapchainManager::shutdown()
{
    if (!engine_)
    {
        return;
    }
    
    cleanupSwapchainResources();
    engine_ = nullptr;
    surface_ = VK_NULL_HANDLE;
}

void SwapchainManager::createSwapchain()
{
    // Defensive checks: ensure engine and surface are valid
    if (!engine_ || !engine_->logicalDevice || !engine_->physicalDevice) {
        throw std::runtime_error("SwapchainManager: Engine or device is not initialized!");
    }
    
    if (surface_ == VK_NULL_HANDLE) {
        throw std::runtime_error("SwapchainManager: Surface is not initialized!");
    }
    
    VkDevice device = engine_->logicalDevice;
    VkPhysicalDevice physicalDevice = engine_->physicalDevice;
    
    vkDeviceWaitIdle(device);

    // Find graphics queue family
    uint32_t graphicsFamilyIndex = UINT32_MAX;
    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    
    if (queueFamilyCount == 0) {
        throw std::runtime_error("SwapchainManager: No queue families available!");
    }
    
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    for (uint32_t i = 0; i < queueFamilyCount; i++)
    {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface_, &presentSupport);
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

    // Create command pool
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(device, &poolInfo, nullptr, &cmdPool_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain command pool!");
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = cmdPool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(device, &allocInfo, &cmdBuffer_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate swapchain command buffer!");
    }

    // Create fence
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    if (vkCreateFence(device, &fenceInfo, nullptr, &fence_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain fence!");
    }

    // Get surface capabilities
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface_, &capabilities);

    // Choose surface format
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface_, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface_, &formatCount, formats.data());

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
    imageFormat_ = surfaceFormat.format;

    // Choose extent
    extent_ = capabilities.currentExtent;
    if (extent_.width == std::numeric_limits<uint32_t>::max())
    {
        extent_.width = std::max(capabilities.minImageExtent.width, 
                                 std::min(extent_.width, capabilities.maxImageExtent.width));
        extent_.height = std::max(capabilities.minImageExtent.height,
                                  std::min(extent_.height, capabilities.maxImageExtent.height));
    }

    // Choose image count
    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    // Create swapchain
    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface_;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = extent_;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &swapchain_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain!");
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, nullptr);
    images_.resize(imageCount);
    vkGetSwapchainImagesKHR(device, swapchain_, &imageCount, images_.data());

    // Create image views
    imageViews_.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++)
    {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(device, &viewInfo, nullptr, &imageViews_[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create image views!");
        }
    }

    // Create render pass, MSAA, depth, framebuffers
    createRenderPass();
    createMsaaResources();
    createDepthResources();
    transitionAttachmentLayouts();
    createFramebuffers();
    
    // Resize per-image fences
    imagesInFlight_.resize(imageCount, VK_NULL_HANDLE);
}

void SwapchainManager::createRenderPass()
{
    VkDevice device = engine_->logicalDevice;

    std::array<VkAttachmentDescription, 3> attachments = {};
    
    // MSAA color attachment
    attachments[0].format = imageFormat_;
    attachments[0].samples = msaaSamples_;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    // Depth attachment
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = msaaSamples_;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    // Resolve attachment (present)
    attachments[2].format = imageFormat_;
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

    if (vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create render pass!");
    }
}

void SwapchainManager::createMsaaResources()
{
    VkDevice device = engine_->logicalDevice;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = imageFormat_;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    imageInfo.samples = msaaSamples_;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &msaaColorImage_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create MSAA color image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, msaaColorImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine_->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &msaaColorMemory_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate MSAA color image memory!");
    }
    vkBindImageMemory(device, msaaColorImage_, msaaColorMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = msaaColorImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageFormat_;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &msaaColorView_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create MSAA color image view!");
    }
}

void SwapchainManager::createDepthResources()
{
    VkDevice device = engine_->logicalDevice;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {extent_.width, extent_.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_D32_SFLOAT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = msaaSamples_;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(device, &imageInfo, nullptr, &depthImage_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(device, depthImage_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine_->findMemoryType(memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(device, &allocInfo, nullptr, &depthMemory_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate depth image memory!");
    }
    vkBindImageMemory(device, depthImage_, depthMemory_, 0);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = depthImage_;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_D32_SFLOAT;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(device, &viewInfo, nullptr, &depthView_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create depth image view!");
    }
}

void SwapchainManager::createFramebuffers()
{
    VkDevice device = engine_->logicalDevice;

    framebuffers_.resize(imageViews_.size());
    for (size_t i = 0; i < imageViews_.size(); i++)
    {
        std::array<VkImageView, 3> attachments = {msaaColorView_, depthView_, imageViews_[i]};

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass_;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = extent_.width;
        framebufferInfo.height = extent_.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers_[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }
}

void SwapchainManager::transitionAttachmentLayouts()
{
    VkDevice device = engine_->logicalDevice;
    VkQueue graphicsQueue = engine_->getGraphicsQueue();

    vkWaitForFences(device, 1, &fence_, VK_TRUE, UINT64_MAX);
    vkResetFences(device, 1, &fence_);
    vkResetCommandBuffer(cmdBuffer_, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(cmdBuffer_, &beginInfo);

    std::array<VkImageMemoryBarrier, 2> barriers{};

    barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barriers[0].newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barriers[0].image = msaaColorImage_;
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
    barriers[1].image = depthImage_;
    barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barriers[1].subresourceRange.baseMipLevel = 0;
    barriers[1].subresourceRange.levelCount = 1;
    barriers[1].subresourceRange.baseArrayLayer = 0;
    barriers[1].subresourceRange.layerCount = 1;
    barriers[1].srcAccessMask = 0;
    barriers[1].dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer_,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        static_cast<uint32_t>(barriers.size()),
        barriers.data());

    vkEndCommandBuffer(cmdBuffer_);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &cmdBuffer_;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, fence_);
}

void SwapchainManager::createSyncObjects()
{
    VkDevice device = engine_->logicalDevice;

    imageAvailableSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    renderFinishedSemaphores_.resize(MAX_FRAMES_IN_FLIGHT);
    inFlightFences_.resize(MAX_FRAMES_IN_FLIGHT);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++)
    {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphores_[i]) != VK_SUCCESS ||
            vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphores_[i]) != VK_SUCCESS ||
            vkCreateFence(device, &fenceInfo, nullptr, &inFlightFences_[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create synchronization objects!");
        }
    }
}

void SwapchainManager::cleanupSwapchainResources()
{
    if (!engine_ || engine_->logicalDevice == VK_NULL_HANDLE)
    {
        return;
    }

    VkDevice device = engine_->logicalDevice;
    VkQueue graphicsQueue = engine_->getGraphicsQueue();

    // Make teardown explicit: wait all known frame fences before touching swapchain-bound resources.
    std::vector<VkFence> fencesToWait;
    fencesToWait.reserve(inFlightFences_.size() + imagesInFlight_.size());
    for (VkFence fence : inFlightFences_)
    {
        if (fence != VK_NULL_HANDLE)
        {
            fencesToWait.push_back(fence);
        }
    }
    for (VkFence fence : imagesInFlight_)
    {
        if (fence != VK_NULL_HANDLE)
        {
            fencesToWait.push_back(fence);
        }
    }
    if (!fencesToWait.empty())
    {
        vkWaitForFences(device,
                        static_cast<uint32_t>(fencesToWait.size()),
                        fencesToWait.data(),
                        VK_TRUE,
                        UINT64_MAX);
    }

    if (graphicsQueue != VK_NULL_HANDLE)
    {
        vkQueueWaitIdle(graphicsQueue);
    }

    const VkResult idleResult = vkDeviceWaitIdle(device);
    if (idleResult != VK_SUCCESS)
    {
        std::cerr << "[SwapchainManager] vkDeviceWaitIdle failed during cleanup: "
                  << static_cast<int>(idleResult) << std::endl;
    }

    // Reset command pool before destroying framebuffers
    if (cmdPool_ != VK_NULL_HANDLE)
    {
        vkResetCommandPool(device, cmdPool_, 0);
    }

    // Destroy framebuffers
    for (auto framebuffer : framebuffers_)
    {
        if (framebuffer != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    framebuffers_.clear();

    // Destroy image views
    for (auto imageView : imageViews_)
    {
        if (imageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(device, imageView, nullptr);
        }
    }
    imageViews_.clear();

    // Destroy MSAA resources
    if (msaaColorView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, msaaColorView_, nullptr);
        msaaColorView_ = VK_NULL_HANDLE;
    }
    if (msaaColorImage_ != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, msaaColorImage_, nullptr);
        msaaColorImage_ = VK_NULL_HANDLE;
    }
    if (msaaColorMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, msaaColorMemory_, nullptr);
        msaaColorMemory_ = VK_NULL_HANDLE;
    }

    // Destroy depth resources
    if (depthView_ != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, depthView_, nullptr);
        depthView_ = VK_NULL_HANDLE;
    }
    if (depthImage_ != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, depthImage_, nullptr);
        depthImage_ = VK_NULL_HANDLE;
    }
    if (depthMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, depthMemory_, nullptr);
        depthMemory_ = VK_NULL_HANDLE;
    }

    // Destroy render pass
    if (renderPass_ != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(device, renderPass_, nullptr);
        renderPass_ = VK_NULL_HANDLE;
    }

    // Destroy swapchain
    if (swapchain_ != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(device, swapchain_, nullptr);
        swapchain_ = VK_NULL_HANDLE;
    }
    images_.clear();

    // Destroy command buffer/pool
    if (cmdBuffer_ != VK_NULL_HANDLE && cmdPool_ != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(device, cmdPool_, 1, &cmdBuffer_);
        cmdBuffer_ = VK_NULL_HANDLE;
    }

    if (cmdPool_ != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(device, cmdPool_, nullptr);
        cmdPool_ = VK_NULL_HANDLE;
    }

    if (fence_ != VK_NULL_HANDLE)
    {
        vkDestroyFence(device, fence_, nullptr);
        fence_ = VK_NULL_HANDLE;
    }

    // Destroy sync objects
    for (auto& sem : renderFinishedSemaphores_)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, sem, nullptr);
            sem = VK_NULL_HANDLE;
        }
    }
    renderFinishedSemaphores_.clear();

    for (auto& sem : imageAvailableSemaphores_)
    {
        if (sem != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(device, sem, nullptr);
            sem = VK_NULL_HANDLE;
        }
    }
    imageAvailableSemaphores_.clear();

    for (auto& fence : inFlightFences_)
    {
        if (fence != VK_NULL_HANDLE)
        {
            vkDestroyFence(device, fence, nullptr);
            fence = VK_NULL_HANDLE;
        }
    }
    inFlightFences_.clear();

    imagesInFlight_.clear();
}

void SwapchainManager::recreateSwapchain(int width, int height)
{
    extent_.width = static_cast<uint32_t>(width);
    extent_.height = static_cast<uint32_t>(height);
    
    cleanupSwapchainResources();
    createSwapchain();
    createSyncObjects();
}

uint32_t SwapchainManager::acquireNextImage(uint32_t currentFrame, uint32_t* imageIndex)
{
    VkDevice device = engine_->logicalDevice;
    if (currentFrame >= inFlightFences_.size() ||
        currentFrame >= imageAvailableSemaphores_.size() ||
        swapchain_ == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Swapchain synchronization state is invalid during image acquisition.");
    }
    
    vkWaitForFences(device, 1, &inFlightFences_[currentFrame], VK_TRUE, UINT64_MAX);
    
    VkResult result = vkAcquireNextImageKHR(
        device, 
        swapchain_, 
        UINT64_MAX, 
        imageAvailableSemaphores_[currentFrame],
        VK_NULL_HANDLE, 
        imageIndex);
    
    return result;
}

VkResult SwapchainManager::presentImage(uint32_t imageIndex, uint32_t currentFrame)
{
    VkQueue graphicsQueue = engine_->getGraphicsQueue();
    
    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores_[currentFrame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain_;
    presentInfo.pImageIndices = &imageIndex;
    
    return vkQueuePresentKHR(graphicsQueue, &presentInfo);
}

void SwapchainManager::setImageInFlight(uint32_t imageIndex, VkFence fence)
{
    if (imageIndex < imagesInFlight_.size())
    {
        imagesInFlight_[imageIndex] = fence;
    }
}
