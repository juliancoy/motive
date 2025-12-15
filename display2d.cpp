#include "display2d.h"
#include "engine.h"
#include "model.h"
#include "utils.h"
#include <stdexcept>
#include <algorithm>
#include <array>
#include <cstddef>
#include <chrono>
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
    uint32_t fpsOverlayEnabled;
    glm::vec2 overlayOrigin;
    glm::vec2 overlaySize;
    glm::vec2 fpsOverlayOrigin;
    glm::vec2 fpsOverlaySize;
    float scrubProgress;
    float scrubPlaying;
};
static_assert(sizeof(ComputePushConstants) == 96, "Compute push constants must match shader layout");
static_assert(offsetof(ComputePushConstants, overlayOrigin) == 56, "overlayOrigin offset mismatch with shader");
static_assert(offsetof(ComputePushConstants, fpsOverlayOrigin) == 72, "fpsOverlayOrigin offset mismatch with shader");

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
    shutdown();
}

void Display2D::shutdown()
{
    if (shutdownPerformed)
    {
        return;
    }

    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();

    // Wait for any in-flight frame work to complete before tearing down swapchain-dependent resources.
    if (!inFlightFences.empty() && engine && engine->logicalDevice != VK_NULL_HANDLE)
    {
        vkWaitForFences(engine->logicalDevice,
                        static_cast<uint32_t>(inFlightFences.size()),
                        inFlightFences.data(),
                        VK_TRUE,
                        UINT64_MAX);
    }
    if (graphicsQueue != VK_NULL_HANDLE)
    {
        vkQueueWaitIdle(graphicsQueue);
    }
    if (engine && engine->logicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(engine->logicalDevice);
    }
    auto tWait = clock::now();

    cleanupSwapchain();
    auto tSwapchain = clock::now();

    if (descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(engine->logicalDevice, descriptorPool, nullptr);
        descriptorPool = VK_NULL_HANDLE;
    }
    if (computePipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(engine->logicalDevice, computePipeline, nullptr);
        computePipeline = VK_NULL_HANDLE;
    }
    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(engine->logicalDevice, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(engine->logicalDevice, descriptorSetLayout, nullptr);
        descriptorSetLayout = VK_NULL_HANDLE;
    }
    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(engine->logicalDevice, commandPool, nullptr);
        commandPool = VK_NULL_HANDLE;
    }
    auto tGpuObjects = clock::now();

    if (surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(engine->instance, surface, nullptr);
        surface = VK_NULL_HANDLE;
    }
    if (window)
    {
        glfwDestroyWindow(window);
        window = nullptr;
    }
    auto tEnd = clock::now();

    auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(tWait - t0).count();
    auto swapchainMs = std::chrono::duration_cast<std::chrono::milliseconds>(tSwapchain - tWait).count();
    auto gpuObjectsMs = std::chrono::duration_cast<std::chrono::milliseconds>(tGpuObjects - tSwapchain).count();
    auto windowMs = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tGpuObjects).count();
    std::cout << "[Display2D] teardown timing: waitAll=" << waitMs
              << " ms, swapchain=" << swapchainMs
              << " ms, gpuObjects=" << gpuObjectsMs
              << " ms, window=" << windowMs << " ms" << std::endl;

    shutdownPerformed = true;
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
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();

    for (auto view : swapchainImageViews)
    {
        vkDestroyImageView(engine->logicalDevice, view, nullptr);
    }
    auto tViews = clock::now();
    swapchainImageViews.clear();
    swapchainImages.clear();

    for (auto fence : inFlightFences)
    {
        vkDestroyFence(engine->logicalDevice, fence, nullptr);
    }
    auto tFences = clock::now();
    for (auto sem : imageAvailableSemaphores)
    {
        vkDestroySemaphore(engine->logicalDevice, sem, nullptr);
    }
    auto tImageSems = clock::now();
    for (auto sem : renderFinishedSemaphores)
    {
        vkDestroySemaphore(engine->logicalDevice, sem, nullptr);
    }
    auto tRenderSems = clock::now();

    imageAvailableSemaphores.clear();
    renderFinishedSemaphores.clear();
    inFlightFences.clear();

    if (swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(engine->logicalDevice, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    auto tSwapchain = clock::now();

    auto viewsMs = std::chrono::duration_cast<std::chrono::milliseconds>(tViews - t0).count();
    auto fencesMs = std::chrono::duration_cast<std::chrono::milliseconds>(tFences - tViews).count();
    auto semAvailMs = std::chrono::duration_cast<std::chrono::milliseconds>(tImageSems - tFences).count();
    auto semRenderMs = std::chrono::duration_cast<std::chrono::milliseconds>(tRenderSems - tImageSems).count();
    auto swapchainMs = std::chrono::duration_cast<std::chrono::milliseconds>(tSwapchain - tRenderSems).count();
    std::cout << "[Display2D] cleanupSwapchain timing: views=" << viewsMs
              << " ms, fences=" << fencesMs
              << " ms, sem(ap)=" << semAvailMs
              << " ms, sem(render)=" << semRenderMs
              << " ms, swapchain=" << swapchainMs << " ms" << std::endl;
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
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    // 0: swapchain storage image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 1: overlay (rectangle)
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 2: fps overlay (text)
    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 2: luma
    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    // 3: chroma
    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

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
    poolSizes[1].descriptorCount = static_cast<uint32_t>(swapchainImages.size()) * 4;

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

void Display2D::renderFrame(const VideoImageSet& videoImages,
                            const OverlayImageInfo& overlayInfo,
                            const OverlayImageInfo& fpsOverlayInfo,
                            const video::VideoColorInfo& colorInfo,
                            float scrubProgress,
                            float scrubPlaying)
{
    if (swapchainImages.empty() || videoImages.luma.view == VK_NULL_HANDLE || videoImages.luma.sampler == VK_NULL_HANDLE)
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

    VkImageMemoryBarrier toRead{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toRead.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toRead.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toRead.image = swapImage;
    toRead.subresourceRange = toGeneral.subresourceRange;
    toRead.srcAccessMask = 0;
    toRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(commandBuffer,
                         VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toRead);

    // Update descriptor set for this image
    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageInfo.imageView = swapchainImageViews[imageIndex];

    VkDescriptorImageInfo overlayImageInfo{};
    overlayImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    if (overlayInfo.enabled && overlayInfo.overlay.view != VK_NULL_HANDLE && overlayInfo.overlay.sampler != VK_NULL_HANDLE)
    {
        overlayImageInfo.imageView = overlayInfo.overlay.view;
        overlayImageInfo.sampler = overlayInfo.overlay.sampler;
    }
    else
    {
        overlayImageInfo.imageView = videoImages.luma.view;
        overlayImageInfo.sampler = videoImages.luma.sampler;
    }
    VkDescriptorImageInfo fpsOverlayImageInfo{};
    fpsOverlayImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    fpsOverlayImageInfo.imageView = (fpsOverlayInfo.overlay.view != VK_NULL_HANDLE) ? fpsOverlayInfo.overlay.view : overlayImageInfo.imageView;
    fpsOverlayImageInfo.sampler = (fpsOverlayInfo.overlay.sampler != VK_NULL_HANDLE) ? fpsOverlayInfo.overlay.sampler : overlayImageInfo.sampler;

    VkDescriptorImageInfo lumaInfo{};
    lumaInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    lumaInfo.imageView = videoImages.luma.view;
    lumaInfo.sampler = videoImages.luma.sampler;

    VkDescriptorImageInfo chromaInfo{};
    chromaInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    chromaInfo.imageView = videoImages.chroma.view ? videoImages.chroma.view : videoImages.luma.view;
    chromaInfo.sampler = videoImages.chroma.sampler ? videoImages.chroma.sampler : videoImages.luma.sampler;

    std::array<VkWriteDescriptorSet, 5> writes{};
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
    writes[1].pImageInfo = &overlayImageInfo;

    // 2: fps overlay
    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = descriptorSets[imageIndex];
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[2].descriptorCount = 1;
    writes[2].pImageInfo = &fpsOverlayImageInfo;

    // 3: luma
    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = descriptorSets[imageIndex];
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[3].descriptorCount = 1;
    writes[3].pImageInfo = &lumaInfo;

    // 4: chroma
    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = descriptorSets[imageIndex];
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].descriptorCount = 1;
    writes[4].pImageInfo = &chromaInfo;

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
    const float videoAspect = videoImages.height > 0 ? static_cast<float>(videoImages.width) / static_cast<float>(videoImages.height) : 1.0f;
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
    push.videoSize = glm::vec2(static_cast<float>(videoImages.width), static_cast<float>(videoImages.height));
    push.targetOrigin = glm::vec2(originX, originY);
    push.targetSize = glm::vec2(targetWidth, targetHeight);
    push.chromaDiv = glm::vec2(static_cast<float>(videoImages.chromaDivX),
                               static_cast<float>(videoImages.chromaDivY));
    push.colorSpace = static_cast<uint32_t>(colorInfo.colorSpace);
    push.colorRange = static_cast<uint32_t>(colorInfo.colorRange);
    // Clamp overlay placement to the current output extent to avoid off-screen coordinates
    uint32_t maxOverlayW = std::max(1u, swapchainExtent.width);
    uint32_t maxOverlayH = std::max(1u, swapchainExtent.height);
    uint32_t clampedOverlayW = std::min(overlayInfo.extent.width, maxOverlayW);
    uint32_t clampedOverlayH = std::min(overlayInfo.extent.height, maxOverlayH);
    int32_t clampedX = std::clamp(overlayInfo.offset.x, 0, static_cast<int32_t>(maxOverlayW - clampedOverlayW));
    int32_t clampedY = std::clamp(overlayInfo.offset.y, 0, static_cast<int32_t>(maxOverlayH - clampedOverlayH));

    uint32_t clampedFpsW = std::min(fpsOverlayInfo.extent.width, maxOverlayW);
    uint32_t clampedFpsH = std::min(fpsOverlayInfo.extent.height, maxOverlayH);
    int32_t clampedFpsX = std::clamp(fpsOverlayInfo.offset.x, 0, static_cast<int32_t>(maxOverlayW - clampedFpsW));
    int32_t clampedFpsY = std::clamp(fpsOverlayInfo.offset.y, 0, static_cast<int32_t>(maxOverlayH - clampedFpsH));

    const bool overlayValid = overlayInfo.enabled &&
                              overlayInfo.overlay.view != VK_NULL_HANDLE &&
                              overlayInfo.overlay.sampler != VK_NULL_HANDLE &&
                              clampedOverlayW > 0 && clampedOverlayH > 0;
    const bool fpsOverlayValid = fpsOverlayInfo.enabled &&
                                 fpsOverlayInfo.overlay.view != VK_NULL_HANDLE &&
                                 fpsOverlayInfo.overlay.sampler != VK_NULL_HANDLE &&
                                 clampedFpsW > 0 && clampedFpsH > 0;
    push.overlayEnabled = overlayValid ? 1u : 0u;
    push.fpsOverlayEnabled = fpsOverlayValid ? 1u : 0u;
    push.scrubProgress = scrubProgress;
    push.scrubPlaying = scrubPlaying;
    push.overlayOrigin = glm::vec2(static_cast<float>(clampedX), static_cast<float>(clampedY));
    push.overlaySize = glm::vec2(static_cast<float>(clampedOverlayW), static_cast<float>(clampedOverlayH));
    push.fpsOverlayOrigin = glm::vec2(static_cast<float>(clampedFpsX), static_cast<float>(clampedFpsY));
    push.fpsOverlaySize = glm::vec2(static_cast<float>(clampedFpsW), static_cast<float>(clampedFpsH));

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
