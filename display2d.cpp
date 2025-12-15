#include "display2d.h"
#include "engine.h"
#include "model.h"
#include "utils.h"
#include <stdexcept>
#include <algorithm>
#include <array>
#include <glm/glm.hpp>

namespace
{
constexpr int kMaxFramesInFlight = 2;

struct ComputePushConstants
{
    glm::vec2 outputSize;
    glm::vec2 videoSize;
    glm::vec2 targetOrigin;
    glm::vec2 targetSize;
    glm::vec2 chromaDiv;
    uint32_t colorSpace;
    uint32_t colorRange;
    uint32_t overlayEnabled;
    glm::vec2 overlayOrigin;
    glm::vec2 overlaySize;
};

VkExtent2D chooseSwapExtent(GLFWwindow* window, const VkSurfaceCapabilitiesKHR& capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX)
    {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(window, &width, &height);

    VkExtent2D actualExtent = {
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height)};

    actualExtent.width = std::max(capabilities.minImageExtent.width,
                                  std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(capabilities.minImageExtent.height,
                                   std::min(capabilities.maxImageExtent.height, actualExtent.height));
    return actualExtent;
}
} // namespace

Display2D::Display2D(Engine* engine, int width, int height, const char* title)
    : engine(engine), width(width), height(height)
{
    if (!engine)
    {
        throw std::runtime_error("Display2D requires a valid engine");
    }
    graphicsQueue = engine->graphicsQueue;
    createWindow(title);
    createSurface();
    createSwapchain();
    createCommandResources();
    createComputeResources();
}

Display2D::~Display2D()
{
    if (engine && engine->logicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(engine->logicalDevice);
    }
    cleanupSwapchain();
    if (descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(engine->logicalDevice, descriptorPool, nullptr);
    }
    if (computePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(engine->logicalDevice, computePipeline, nullptr);
    }
    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(engine->logicalDevice, pipelineLayout, nullptr);
    }
    if (descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(engine->logicalDevice, descriptorSetLayout, nullptr);
    }
    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(engine->logicalDevice, commandPool, nullptr);
    }
    if (surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(engine->instance, surface, nullptr);
    }
    if (window)
    {
        glfwDestroyWindow(window);
    }
}

void Display2D::createWindow(const char* title)
{
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        throw std::runtime_error("Failed to create GLFW window for Display2D");
    }
}

void Display2D::createSurface()
{
    if (glfwCreateWindowSurface(engine->instance, window, nullptr, &surface) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create window surface for Display2D");
    }
}

void Display2D::createSwapchain()
{
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(engine->physicalDevice, surface, &capabilities) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to query surface capabilities for Display2D");
    }

    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(engine->physicalDevice, surface, &formatCount, formats.data());

    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& fmt : formats)
    {
        if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB && fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
        {
            surfaceFormat = fmt;
            break;
        }
    }

    swapchainFormat = surfaceFormat.format;
    swapchainExtent = chooseSwapExtent(window, capabilities);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount)
    {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    createInfo.surface = surface;
    createInfo.minImageCount = imageCount;
    createInfo.imageFormat = swapchainFormat;
    createInfo.imageColorSpace = surfaceFormat.colorSpace;
    createInfo.imageExtent = swapchainExtent;
    createInfo.imageArrayLayers = 1;
    createInfo.imageUsage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    // Single queue family path
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.preTransform = capabilities.currentTransform;
    createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    createInfo.clipped = VK_TRUE;

    if (vkCreateSwapchainKHR(engine->logicalDevice, &createInfo, nullptr, &swapchain) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create swapchain for Display2D");
    }

    vkGetSwapchainImagesKHR(engine->logicalDevice, swapchain, &imageCount, nullptr);
    swapchainImages.resize(imageCount);
    vkGetSwapchainImagesKHR(engine->logicalDevice, swapchain, &imageCount, swapchainImages.data());

    swapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < swapchainImages.size(); ++i)
    {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(engine->logicalDevice, &viewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create swapchain image view for Display2D");
        }
    }

    imageAvailableSemaphores.resize(kMaxFramesInFlight);
    renderFinishedSemaphores.resize(kMaxFramesInFlight);
    inFlightFences.resize(kMaxFramesInFlight);

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (int i = 0; i < kMaxFramesInFlight; ++i)
    {
        if (vkCreateSemaphore(engine->logicalDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
            vkCreateSemaphore(engine->logicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
            vkCreateFence(engine->logicalDevice, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create sync objects for Display2D");
        }
    }
}

void Display2D::cleanupSwapchain()
{
    for (auto view : swapchainImageViews)
    {
        vkDestroyImageView(engine->logicalDevice, view, nullptr);
    }
    swapchainImageViews.clear();
    swapchainImages.clear();

    for (auto fence : inFlightFences)
    {
        vkDestroyFence(engine->logicalDevice, fence, nullptr);
    }
    for (auto sem : imageAvailableSemaphores)
    {
        vkDestroySemaphore(engine->logicalDevice, sem, nullptr);
    }
    for (auto sem : renderFinishedSemaphores)
    {
        vkDestroySemaphore(engine->logicalDevice, sem, nullptr);
    }

    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();

    if (swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(engine->logicalDevice, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
}

void Display2D::createCommandResources()
{
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = engine->graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    if (vkCreateCommandPool(engine->logicalDevice, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool for Display2D");
    }

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(engine->logicalDevice, &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffer for Display2D");
    }
}

void Display2D::createComputeResources()
{
    // Descriptor set layout
    std::array<VkDescriptorSetLayoutBinding, 4> bindings{};
    // 0: swapchain storage image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 1: overlay
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 2: luma
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 3: chroma
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(engine->logicalDevice, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor set layout for Display2D");
    }

    // Pipeline layout with push constants
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(ComputePushConstants);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(engine->logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout for Display2D");
    }

    // Compute pipeline
    auto shaderCode = readSPIRVFile("shaders/video_blit.comp.spv");
    VkShaderModule shaderModule = engine->createShaderModule(shaderCode);

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout;

    if (vkCreateComputePipelines(engine->logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &computePipeline) != VK_SUCCESS)
    {
        vkDestroyShaderModule(engine->logicalDevice, shaderModule, nullptr);
        throw std::runtime_error("Failed to create compute pipeline for Display2D");
    }

    vkDestroyShaderModule(engine->logicalDevice, shaderModule, nullptr);

    // Descriptor pool and sets (one per swapchain image)
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = static_cast<uint32_t>(swapchainImages.size());
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = static_cast<uint32_t>(swapchainImages.size()) * 3;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = static_cast<uint32_t>(swapchainImages.size());

    if (vkCreateDescriptorPool(engine->logicalDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor pool for Display2D");
    }

    std::vector<VkDescriptorSetLayout> layouts(swapchainImages.size(), descriptorSetLayout);
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = descriptorPool;
    allocInfo.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    allocInfo.pSetLayouts = layouts.data();

    descriptorSets.resize(layouts.size());
    if (vkAllocateDescriptorSets(engine->logicalDevice, &allocInfo, descriptorSets.data()) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor sets for Display2D");
    }
}

void Display2D::recreateSwapchain()
{
    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
    while (fbWidth == 0 || fbHeight == 0)
    {
        glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
        glfwWaitEvents();
    }
    vkDeviceWaitIdle(engine->logicalDevice);

    if (descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(engine->logicalDevice, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(engine->logicalDevice, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }

    cleanupSwapchain();
    createSwapchain();
    createCommandResources();
    createComputeResources();
}

bool Display2D::shouldClose() const
{
    return glfwWindowShouldClose(window);
}

void Display2D::pollEvents() const
{
    glfwPollEvents();
}

void Display2D::renderFrame(Primitive* videoPrimitive,
                            Primitive* overlayPrimitive,
                            const video::VideoColorInfo& colorInfo,
                            uint32_t videoWidth,
                            uint32_t videoHeight,
                            VkExtent2D overlaySize,
                            VkOffset2D overlayOffset)
{
    if (!videoPrimitive || swapchainImages.empty())
    {
        return;
    }

    vkWaitForFences(engine->logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex = 0;
    VkResult acquire = vkAcquireNextImageKHR(engine->logicalDevice,
                                             swapchain,
                                             UINT64_MAX,
                                             imageAvailableSemaphores[currentFrame],
                                             VK_NULL_HANDLE,
                                             &imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreateSwapchain();
        return;
    }

    vkResetFences(engine->logicalDevice, 1, &inFlightFences[currentFrame]);
    vkResetCommandBuffer(commandBuffer, 0);

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImage swapImage = swapchainImages[imageIndex];
    VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneral.image = swapImage;
    toGeneral.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toGeneral.subresourceRange.baseMipLevel = 0;
    toGeneral.subresourceRange.levelCount = 1;
    toGeneral.subresourceRange.baseArrayLayer = 0;
    toGeneral.subresourceRange.layerCount = 1;
    toGeneral.srcAccessMask = 0;
    toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toGeneral);

    // Update descriptor set for this image
    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageInfo.imageView = swapchainImageViews[imageIndex];

    VkDescriptorImageInfo lumaInfo{};
    lumaInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lumaInfo.imageView = videoPrimitive->textureImageView;
    lumaInfo.sampler = videoPrimitive->textureSampler;

    VkDescriptorImageInfo chromaInfo{};
    chromaInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    chromaInfo.imageView = videoPrimitive->chromaImageView ? videoPrimitive->chromaImageView : videoPrimitive->textureImageView;
    chromaInfo.sampler = videoPrimitive->textureSampler;

    VkDescriptorImageInfo overlayInfo{};
    overlayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (overlayPrimitive && overlayPrimitive->textureImageView)
    {
        overlayInfo.imageView = overlayPrimitive->textureImageView;
        overlayInfo.sampler = overlayPrimitive->textureSampler;
    }
    else
    {
        overlayInfo.imageView = videoPrimitive->textureImageView;
        overlayInfo.sampler = videoPrimitive->textureSampler;
    }

    std::array<VkWriteDescriptorSet, 4> writes{};
    // 0: storage (swapchain)
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = descriptorSets[imageIndex];
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &storageInfo;

    // 1: overlay
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = descriptorSets[imageIndex];
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 1;
    writes[1].pImageInfo = &overlayInfo;

    // 2: luma
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSets[imageIndex];
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &lumaInfo;

    // 3: chroma
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descriptorSets[imageIndex];
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &chromaInfo;

    vkUpdateDescriptorSets(engine->logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
    vkCmdBindDescriptorSets(commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            pipelineLayout,
                            0,
                            1,
                            &descriptorSets[imageIndex],
                            0,
                            nullptr);

    const float outputAspect = static_cast<float>(swapchainExtent.width) / std::max(1u, swapchainExtent.height);
    const float videoAspect = videoHeight > 0 ? static_cast<float>(videoWidth) / static_cast<float>(videoHeight) : 1.0f;
    float targetWidth = static_cast<float>(swapchainExtent.width);
    float targetHeight = static_cast<float>(swapchainExtent.height);
    if (videoAspect > outputAspect)
    {
        targetHeight = targetWidth / videoAspect;
    }
    else
    {
        targetWidth = targetHeight * videoAspect;
    }
    const float originX = (static_cast<float>(swapchainExtent.width) - targetWidth) * 0.5f;
    const float originY = (static_cast<float>(swapchainExtent.height) - targetHeight) * 0.5f;

    ComputePushConstants push{};
    push.outputSize = glm::vec2(static_cast<float>(swapchainExtent.width), static_cast<float>(swapchainExtent.height));
    push.videoSize = glm::vec2(static_cast<float>(videoWidth), static_cast<float>(videoHeight));
    push.targetOrigin = glm::vec2(originX, originY);
    push.targetSize = glm::vec2(targetWidth, targetHeight);
    push.chromaDiv = glm::vec2(static_cast<float>(videoPrimitive->yuvChromaDivX),
                               static_cast<float>(videoPrimitive->yuvChromaDivY));
    push.colorSpace = static_cast<uint32_t>(colorInfo.colorSpace);
    push.colorRange = static_cast<uint32_t>(colorInfo.colorRange);
    // Clamp overlay placement to the current output extent to avoid off-screen coordinates
    uint32_t maxOverlayW = std::max(1u, swapchainExtent.width);
    uint32_t maxOverlayH = std::max(1u, swapchainExtent.height);
    uint32_t clampedOverlayW = std::min(overlaySize.width, maxOverlayW);
    uint32_t clampedOverlayH = std::min(overlaySize.height, maxOverlayH);
    int32_t clampedX = std::clamp(overlayOffset.x, 0, static_cast<int32_t>(maxOverlayW - clampedOverlayW));
    int32_t clampedY = std::clamp(overlayOffset.y, 0, static_cast<int32_t>(maxOverlayH - clampedOverlayH));

    push.overlayEnabled = (overlayPrimitive && overlayPrimitive->textureImageView && clampedOverlayW > 0 && clampedOverlayH > 0) ? 1u : 0u;
    // Debug
    static int debugFrame = 0;
    if (debugFrame++ % 60 == 0) {
        std::cout << "[Display2D] overlayEnabled=" << push.overlayEnabled 
                  << ", textureImageView=" << (overlayPrimitive ? overlayPrimitive->textureImageView : 0)
                  << ", clampedOverlay=" << clampedOverlayW << "x" << clampedOverlayH
                  << ", origin=" << clampedX << "," << clampedY << std::endl;
    }
    push.overlayOrigin = glm::vec2(static_cast<float>(clampedX), static_cast<float>(clampedY));
    push.overlaySize = glm::vec2(static_cast<float>(clampedOverlayW), static_cast<float>(clampedOverlayH));

    vkCmdPushConstants(commandBuffer, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &push);

    const uint32_t groupX = (swapchainExtent.width + 15) / 16;
    const uint32_t groupY = (swapchainExtent.height + 15) / 16;
    vkCmdDispatch(commandBuffer, groupX, groupY, 1);

    VkImageMemoryBarrier toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toPresent.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = swapImage;
    toPresent.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toPresent.subresourceRange.baseMipLevel = 0;
    toPresent.subresourceRange.levelCount = 1;
    toPresent.subresourceRange.baseArrayLayer = 0;
    toPresent.subresourceRange.layerCount = 1;
    toPresent.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toPresent.dstAccessMask = 0;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toPresent);

    vkEndCommandBuffer(commandBuffer);

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &renderFinishedSemaphores[currentFrame];

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit compute work for Display2D");
    }

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &renderFinishedSemaphores[currentFrame];
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    VkResult presentResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR)
    {
        recreateSwapchain();
    }

    currentFrame = (currentFrame + 1) % kMaxFramesInFlight;
}
