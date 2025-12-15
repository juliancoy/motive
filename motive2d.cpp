#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <string>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

#include <vulkan/vulkan.h>

#include "display2d.h"
#include "engine.h"
#include "glyph.h"
#include "light.h"
#include "utils.h"
#include "video.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace
{
// Look for the sample video in the current directory (files were moved up).
const std::filesystem::path kVideoPath = std::filesystem::path("P1090533_main8_hevc_fast.mkv");
constexpr uint32_t kScrubberWidth = 512;
constexpr uint32_t kScrubberHeight = 64;

struct ImageResource
{
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct VideoResources
{
    VideoImageSet descriptors;
    ImageResource lumaImage;
    ImageResource chromaImage;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct OverlayResources
{
    OverlayImageInfo info;
    ImageResource image;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct FpsOverlayResources
{
    OverlayImageInfo info;
    ImageResource image;
    float lastFpsValue = -1.0f;
    uint32_t lastRefWidth = 0;
    uint32_t lastRefHeight = 0;
};

struct OverlayCompute
{
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
};

struct VideoPlaybackState
{
    Engine* engine = nullptr;
    video::VideoDecoder decoder;
    VideoResources video;
    OverlayResources overlay;
    FpsOverlayResources fpsOverlay;
    video::VideoColorInfo colorInfo;
    std::deque<video::DecodedFrame> pendingFrames;
    video::DecodedFrame stagingFrame;
    bool playbackClockInitialized = false;
    double basePtsSeconds = 0.0;
    double lastFramePtsSeconds = 0.0;
    double lastDisplayedSeconds = 0.0;
    std::chrono::steady_clock::time_point lastFrameRenderTime{};
};

enum class DebugCategory
{
    Decode,
    Cleanup,
    Overlay
};

struct DebugFlags
{
    bool decode = false;
    bool cleanup = false;
    bool overlay = false;
} gDebugFlags;

bool isDebugEnabled(DebugCategory category)
{
    switch (category)
    {
    case DebugCategory::Decode:
        return gDebugFlags.decode;
    case DebugCategory::Cleanup:
        return gDebugFlags.cleanup;
    case DebugCategory::Overlay:
        return gDebugFlags.overlay;
    default:
        return false;
    }
}

void parseDebugFlags(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg(argv[i] ? argv[i] : "");
        if (arg == "--debugDecode")
        {
            gDebugFlags.decode = true;
        }
        else if (arg == "--debugCleanup")
        {
            gDebugFlags.cleanup = true;
        }
        else if (arg == "--debugOverlay")
        {
            gDebugFlags.overlay = true;
        }
        else if (arg == "--debugAll")
        {
            gDebugFlags.decode = gDebugFlags.cleanup = gDebugFlags.overlay = true;
        }
    }
}

bool uploadDecodedFrame(VideoResources& video,
                        Engine* engine,
                        const video::VideoDecoder& decoder,
                        const video::DecodedFrame& frame);
bool uploadImageData(Engine* engine,
                     ImageResource& res,
                     const void* data,
                     size_t dataSize,
                     uint32_t width,
                     uint32_t height,
                     VkFormat format);

void updateFpsOverlay(VideoPlaybackState& state, float fpsValue, uint32_t fbWidth, uint32_t fbHeight)
{
    state.fpsOverlay.lastFpsValue = fpsValue;
    state.fpsOverlay.lastRefWidth = fbWidth;
    state.fpsOverlay.lastRefHeight = fbHeight;

    glyph::OverlayBitmap bitmap = glyph::buildFrameRateOverlay(fbWidth, fbHeight, fpsValue);
    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.pixels.empty())
    {
        state.fpsOverlay.info.enabled = false;
        return;
    }

    if (!uploadImageData(state.engine,
                         state.fpsOverlay.image,
                         bitmap.pixels.data(),
                         bitmap.pixels.size(),
                         bitmap.width,
                         bitmap.height,
                         VK_FORMAT_R8G8B8A8_UNORM))
    {
        std::cerr << "[Video2D] Failed to upload FPS overlay image." << std::endl;
        state.fpsOverlay.info.enabled = false;
        return;
    }

    state.fpsOverlay.info.overlay.view = state.fpsOverlay.image.view;
    state.fpsOverlay.info.overlay.sampler = (state.overlay.sampler != VK_NULL_HANDLE) ? state.overlay.sampler : state.video.sampler;
    state.fpsOverlay.info.extent = {bitmap.width, bitmap.height};
    state.fpsOverlay.info.offset = {static_cast<int32_t>(bitmap.offsetX), static_cast<int32_t>(bitmap.offsetY)};
    state.fpsOverlay.info.enabled = true;
}

void destroyOverlayCompute(OverlayCompute& comp)
{
    if (comp.fence != VK_NULL_HANDLE)
    {
        vkDestroyFence(comp.device, comp.fence, nullptr);
        comp.fence = VK_NULL_HANDLE;
    }
    if (comp.commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(comp.device, comp.commandPool, nullptr);
        comp.commandPool = VK_NULL_HANDLE;
    }
    if (comp.descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(comp.device, comp.descriptorPool, nullptr);
        comp.descriptorPool = VK_NULL_HANDLE;
    }
    if (comp.pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(comp.device, comp.pipeline, nullptr);
        comp.pipeline = VK_NULL_HANDLE;
    }
    if (comp.pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(comp.device, comp.pipelineLayout, nullptr);
        comp.pipelineLayout = VK_NULL_HANDLE;
    }
    if (comp.descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(comp.device, comp.descriptorSetLayout, nullptr);
        comp.descriptorSetLayout = VK_NULL_HANDLE;
    }
}

void destroyImageResource(Engine* engine, ImageResource& res)
{
    if (!engine)
    {
        return;
    }
    if (res.view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, res.view, nullptr);
        res.view = VK_NULL_HANDLE;
    }
    if (res.image != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, res.image, nullptr);
        res.image = VK_NULL_HANDLE;
    }
    if (res.memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, res.memory, nullptr);
        res.memory = VK_NULL_HANDLE;
    }
    res.format = VK_FORMAT_UNDEFINED;
    res.width = 0;
    res.height = 0;
}

VkSampler createLinearClampSampler(Engine* engine)
{
    VkSampler sampler = VK_NULL_HANDLE;
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = engine->props.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(engine->logicalDevice, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create sampler.");
    }
    return sampler;
}

bool ensureImageResource(Engine* engine,
                         ImageResource& res,
                         uint32_t width,
                         uint32_t height,
                         VkFormat format,
                         bool& recreated,
                         VkImageUsageFlags usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
{
    recreated = false;
    if (res.image != VK_NULL_HANDLE && res.width == width && res.height == height && res.format == format)
    {
        return true;
    }

    destroyImageResource(engine, res);
    recreated = true;

    if (width == 0 || height == 0 || format == VK_FORMAT_UNDEFINED)
    {
        return false;
    }

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent = {width, height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = usage;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(engine->logicalDevice, &imageInfo, nullptr, &res.image) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create image." << std::endl;
        return false;
    }

    VkMemoryRequirements memReq{};
    vkGetImageMemoryRequirements(engine->logicalDevice, res.image, &memReq);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memReq.size;
    allocInfo.memoryTypeIndex = engine->findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &res.memory) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to allocate image memory." << std::endl;
        vkDestroyImage(engine->logicalDevice, res.image, nullptr);
        res.image = VK_NULL_HANDLE;
        return false;
    }

    vkBindImageMemory(engine->logicalDevice, res.image, res.memory, 0);

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = res.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(engine->logicalDevice, &viewInfo, nullptr, &res.view) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create image view." << std::endl;
        destroyImageResource(engine, res);
        return false;
    }

    res.format = format;
    res.width = width;
    res.height = height;
    return true;
}

void copyBufferToImage(Engine* engine,
                       VkBuffer stagingBuffer,
                       VkImage targetImage,
                       VkImageLayout currentLayout,
                       uint32_t width,
                       uint32_t height)
{
    VkCommandBuffer cmd = engine->beginSingleTimeCommands();

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = currentLayout;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = targetImage;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = 0;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = (currentLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : VK_ACCESS_SHADER_READ_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    VkPipelineStageFlags srcStage = (currentLayout == VK_IMAGE_LAYOUT_UNDEFINED)
                                        ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT
                                        : VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

    vkCmdPipelineBarrier(cmd,
                         srcStage,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toTransfer);

    VkBufferImageCopy copy{};
    copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    copy.imageSubresource.mipLevel = 0;
    copy.imageSubresource.baseArrayLayer = 0;
    copy.imageSubresource.layerCount = 1;
    copy.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(cmd, stagingBuffer, targetImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);

    VkImageMemoryBarrier toShader{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toShader.image = targetImage;
    toShader.subresourceRange = toTransfer.subresourceRange;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(cmd,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toShader);

    engine->endSingleTimeCommands(cmd);
}

bool uploadImageData(Engine* engine,
                     ImageResource& res,
                     const void* data,
                     size_t dataSize,
                     uint32_t width,
                     uint32_t height,
                     VkFormat format)
{
    if (!data || dataSize == 0 || width == 0 || height == 0)
    {
        return false;
    }

    bool recreated = false;
    if (!ensureImageResource(engine, res, width, height, format, recreated))
    {
        return false;   
    }

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    engine->createBuffer(static_cast<VkDeviceSize>(dataSize),
                         VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                         VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                         staging,
                         stagingMemory);

    void* mapped = nullptr;
    vkMapMemory(engine->logicalDevice, stagingMemory, 0, static_cast<VkDeviceSize>(dataSize), 0, &mapped);
    std::memcpy(mapped, data, dataSize);
    vkUnmapMemory(engine->logicalDevice, stagingMemory);

    VkImageLayout oldLayout = (!recreated && res.view != VK_NULL_HANDLE)
                                  ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                                  : VK_IMAGE_LAYOUT_UNDEFINED;
    copyBufferToImage(engine, staging, res.image, oldLayout, width, height);

    vkDestroyBuffer(engine->logicalDevice, staging, nullptr);
    vkFreeMemory(engine->logicalDevice, stagingMemory, nullptr);
    return true;
}

void runOverlayCompute(Engine* engine,
                       OverlayCompute& comp,
                       ImageResource& target,
                       uint32_t width,
                       uint32_t height,
                       const glm::vec2& rectCenter,
                       const glm::vec2& rectSize,
                       float outerThickness,
                       float innerThickness)
{
    // Ensure storage-capable overlay image
    bool recreated = false;
    if (!ensureImageResource(engine, target, width, height, VK_FORMAT_R8G8B8A8_UNORM, recreated,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        return;
    }

    comp.width = width;
    comp.height = height;

    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageInfo.imageView = target.view;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = comp.descriptorSet;
    write.dstBinding = 0;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.descriptorCount = 1;
    write.pImageInfo = &storageInfo;
    vkUpdateDescriptorSets(comp.device, 1, &write, 0, nullptr);

    vkResetCommandBuffer(comp.commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(comp.commandBuffer, &beginInfo);

    struct OverlayPush {
        glm::vec2 outputSize;
        glm::vec2 rectCenter;
        glm::vec2 rectSize;
        float outerThickness;
        float innerThickness;
    } push{glm::vec2(static_cast<float>(width), static_cast<float>(height)), rectCenter, rectSize, outerThickness, innerThickness};

    const VkImageLayout initialLayout = recreated ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    // Transition to GENERAL for storage write
    VkImageMemoryBarrier toGeneralBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toGeneralBarrier.oldLayout = initialLayout;
    toGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.image = target.image;
    toGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toGeneralBarrier.subresourceRange.baseMipLevel = 0;
    toGeneralBarrier.subresourceRange.levelCount = 1;
    toGeneralBarrier.subresourceRange.baseArrayLayer = 0;
    toGeneralBarrier.subresourceRange.layerCount = 1;
    toGeneralBarrier.srcAccessMask = (initialLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                         ? VK_ACCESS_SHADER_READ_BIT
                                         : 0;
    toGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    VkPipelineStageFlags srcStage = (initialLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                        ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    vkCmdPipelineBarrier(comp.commandBuffer,
                         srcStage,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toGeneralBarrier);

    vkCmdBindPipeline(comp.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, comp.pipeline);
    vkCmdBindDescriptorSets(comp.commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            comp.pipelineLayout,
                            0,
                            1,
                            &comp.descriptorSet,
                            0,
                            nullptr);
    vkCmdPushConstants(comp.commandBuffer,
                       comp.pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(OverlayPush),
                       &push);

    const uint32_t groupX = (width + 15) / 16;
    const uint32_t groupY = (height + 15) / 16;
    vkCmdDispatch(comp.commandBuffer, groupX, groupY, 1);

    // Transition to SHADER_READ_ONLY for sampling in main pass
    VkImageMemoryBarrier toReadBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.image = target.image;
    toReadBarrier.subresourceRange = toGeneralBarrier.subresourceRange;
    toReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(comp.commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toReadBarrier);

    vkEndCommandBuffer(comp.commandBuffer);

    vkResetFences(comp.device, 1, &comp.fence);
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &comp.commandBuffer;
    vkQueueSubmit(engine->graphicsQueue, 1, &submitInfo, comp.fence);
    vkWaitForFences(comp.device, 1, &comp.fence, VK_TRUE, UINT64_MAX);
}
bool initializeOverlayCompute(Engine* engine, OverlayCompute& comp)
{
    comp.device = engine->logicalDevice;
    comp.queue = engine->graphicsQueue;

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(comp.device, &layoutInfo, nullptr, &comp.descriptorSetLayout) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create overlay descriptor set layout" << std::endl;
        return false;
    }

    std::vector<char> shaderCode;
    try
    {
        shaderCode = readSPIRVFile("shaders/overlay_rect.comp.spv");
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[Video2D] " << ex.what() << std::endl;
        destroyOverlayCompute(comp);
        return false;
    }

    VkShaderModule shaderModule = engine->createShaderModule(shaderCode);

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(glm::vec2) * 3 + sizeof(float) * 2; // outputSize, rectCenter, rectSize, outer, inner

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &comp.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(comp.device, &pipelineLayoutInfo, nullptr, &comp.pipelineLayout) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create overlay pipeline layout" << std::endl;
        vkDestroyShaderModule(comp.device, shaderModule, nullptr);
        destroyOverlayCompute(comp);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = comp.pipelineLayout;

    if (vkCreateComputePipelines(comp.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &comp.pipeline) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create overlay compute pipeline" << std::endl;
        vkDestroyShaderModule(comp.device, shaderModule, nullptr);
        destroyOverlayCompute(comp);
        return false;
    }

    vkDestroyShaderModule(comp.device, shaderModule, nullptr);

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSize.descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;

    if (vkCreateDescriptorPool(comp.device, &poolInfo, nullptr, &comp.descriptorPool) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create overlay descriptor pool" << std::endl;
        destroyOverlayCompute(comp);
        return false;
    }

    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = comp.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &comp.descriptorSetLayout;

    if (vkAllocateDescriptorSets(comp.device, &allocInfo, &comp.descriptorSet) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to allocate overlay descriptor set" << std::endl;
        destroyOverlayCompute(comp);
        return false;
    }

    VkCommandPoolCreateInfo poolCreateInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCreateInfo.queueFamilyIndex = engine->graphicsQueueFamilyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(comp.device, &poolCreateInfo, nullptr, &comp.commandPool) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create overlay command pool" << std::endl;
        destroyOverlayCompute(comp);
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = comp.commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(comp.device, &cmdAllocInfo, &comp.commandBuffer) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to allocate overlay command buffer" << std::endl;
        destroyOverlayCompute(comp);
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(comp.device, &fenceInfo, nullptr, &comp.fence) != VK_SUCCESS)
    {
        std::cerr << "[Video2D] Failed to create overlay fence" << std::endl;
        destroyOverlayCompute(comp);
        return false;
    }

    return true;
}

// Scroll handling for rectangle sizing
static double g_scrollDelta = 0.0;
static void onScroll(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset)
{
    g_scrollDelta += yoffset;
}

static void onKey(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    /*if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        std::cout << "[Video2D] ESC pressed at "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count()
                  << " ms" << std::endl;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }*/
}

bool initializeVideoPlayback(const std::filesystem::path& videoPath,
                             Engine* engine,
                             VideoPlaybackState& state,
                             double& durationSeconds)
{
    state.engine = engine;
    if (!std::filesystem::exists(videoPath))
    {
        std::cerr << "[Video2D] Missing video file: " << videoPath << std::endl;
        return false;
    }

    video::DecoderInitParams params{};
    params.implementation = video::DecodeImplementation::Vulkan;
    if (!video::initializeVideoDecoder(videoPath, state.decoder, params))
    {
        std::cerr << "[Video2D] Vulkan decode unavailable, falling back to software." << std::endl;
        params.implementation = video::DecodeImplementation::Software;
        if (!video::initializeVideoDecoder(videoPath, state.decoder, params))
        {
            std::cerr << "[Video2D] Failed to initialize decoder" << std::endl;
            return false;
        }
    }
    if (state.decoder.implementation != video::DecodeImplementation::Vulkan)
    {
        std::cerr << "[Video2D] Warning: hardware Vulkan decode not active; using "
                  << state.decoder.implementationName << std::endl;
    }

    durationSeconds = 0.0;
    if (state.decoder.formatCtx && state.decoder.formatCtx->duration > 0)
    {
        durationSeconds = static_cast<double>(state.decoder.formatCtx->duration) / static_cast<double>(AV_TIME_BASE);
    }

    state.colorInfo = video::deriveVideoColorInfo(state.decoder);

    try
    {
        state.video.sampler = createLinearClampSampler(engine);
        state.overlay.sampler = createLinearClampSampler(engine);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[Video2D] Failed to create samplers: " << ex.what() << std::endl;
        video::cleanupVideoDecoder(state.decoder);
        return false;
    }

    video::DecodedFrame initialDecoded{};
    initialDecoded.buffer.assign(static_cast<size_t>(state.decoder.bufferSize), 0);
    if (state.decoder.outputFormat == PrimitiveYuvFormat::NV12)
    {
        const size_t yBytes = state.decoder.yPlaneBytes;
        if (yBytes > 0 && yBytes <= initialDecoded.buffer.size())
        {
            std::fill(initialDecoded.buffer.begin(), initialDecoded.buffer.begin() + yBytes, 0x80);
            std::fill(initialDecoded.buffer.begin() + yBytes, initialDecoded.buffer.end(), 0x80);
        }
    }
    else
    {
        const size_t yBytes = state.decoder.yPlaneBytes;
        const size_t uvBytes = state.decoder.uvPlaneBytes;
        const bool sixteenBit = state.decoder.bytesPerComponent > 1;
        if (sixteenBit)
        {
            const uint32_t bitDepth = state.decoder.bitDepth > 0 ? state.decoder.bitDepth : 8;
            const uint32_t shift = bitDepth >= 16 ? 0u : 16u - bitDepth;
            const uint16_t baseValue = static_cast<uint16_t>(1u << (bitDepth > 0 ? bitDepth - 1 : 7));
            const uint16_t fillValue = static_cast<uint16_t>(baseValue << shift);
            if (yBytes >= sizeof(uint16_t))
            {
                uint16_t* yDst = reinterpret_cast<uint16_t*>(initialDecoded.buffer.data());
                std::fill(yDst, yDst + (yBytes / sizeof(uint16_t)), fillValue);
            }
            if (uvBytes >= sizeof(uint16_t))
            {
                uint16_t* uvDst = reinterpret_cast<uint16_t*>(initialDecoded.buffer.data() + yBytes);
                std::fill(uvDst, uvDst + (uvBytes / sizeof(uint16_t)), fillValue);
            }
        }
        else
        {
            if (yBytes > 0 && yBytes <= initialDecoded.buffer.size())
            {
                std::fill(initialDecoded.buffer.begin(), initialDecoded.buffer.begin() + yBytes, 0x80);
            }
            if (uvBytes > 0 && yBytes + uvBytes <= initialDecoded.buffer.size())
            {
                std::fill(initialDecoded.buffer.begin() + yBytes, initialDecoded.buffer.begin() + yBytes + uvBytes, 0x80);
            }
        }
    }

    if (!uploadDecodedFrame(state.video, engine, state.decoder, initialDecoded))
    {
        std::cerr << "[Video2D] Failed to upload initial frame." << std::endl;
        video::cleanupVideoDecoder(state.decoder);
        destroyImageResource(engine, state.video.lumaImage);
        destroyImageResource(engine, state.video.chromaImage);
        if (state.video.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(engine->logicalDevice, state.video.sampler, nullptr);
            state.video.sampler = VK_NULL_HANDLE;
        }
        if (state.overlay.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(engine->logicalDevice, state.overlay.sampler, nullptr);
            state.overlay.sampler = VK_NULL_HANDLE;
        }
        return false;
    }

    if (!video::startAsyncDecoding(state.decoder, 12))
    {
        std::cerr << "[Video2D] Failed to start async decoder" << std::endl;
        video::cleanupVideoDecoder(state.decoder);
        destroyImageResource(engine, state.video.lumaImage);
        destroyImageResource(engine, state.video.chromaImage);
        if (state.video.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(engine->logicalDevice, state.video.sampler, nullptr);
            state.video.sampler = VK_NULL_HANDLE;
        }
        if (state.overlay.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(engine->logicalDevice, state.overlay.sampler, nullptr);
            state.overlay.sampler = VK_NULL_HANDLE;
        }
        return false;
    }
    if (isDebugEnabled(DebugCategory::Decode))
    {
        std::cout << "[Video2D] Loaded video " << videoPath
                  << " (" << state.decoder.width << "x" << state.decoder.height
                  << "), fps=" << state.decoder.fps << std::endl;
    }

    state.stagingFrame.buffer.reserve(static_cast<size_t>(state.decoder.bufferSize));
    state.pendingFrames.clear();
    state.playbackClockInitialized = false;
    state.lastDisplayedSeconds = 0.0;
    return true;
}

void pumpDecodedFrames(VideoPlaybackState& state)
{
    constexpr size_t kMaxPendingFrames = 6;
    while (state.pendingFrames.size() < kMaxPendingFrames &&
           video::acquireDecodedFrame(state.decoder, state.stagingFrame))
    {
        state.pendingFrames.emplace_back(std::move(state.stagingFrame));
        state.stagingFrame = video::DecodedFrame{};
        state.stagingFrame.buffer.reserve(static_cast<size_t>(state.decoder.bufferSize));
    }
}

bool uploadDecodedFrame(VideoResources& video,
                        Engine* engine,
                        const video::VideoDecoder& decoder,
                        const video::DecodedFrame& frame)
{
    if (!engine || frame.buffer.empty())
    {
        return false;
    }

    if (decoder.outputFormat == PrimitiveYuvFormat::NV12)
    {
        const size_t ySize = decoder.yPlaneBytes;
        const size_t uvSize = decoder.uvPlaneBytes;
        if (frame.buffer.size() < ySize + uvSize)
        {
            std::cerr << "[Video2D] NV12 frame smaller than expected." << std::endl;
            return false;
        }
        const uint8_t* yPlane = frame.buffer.data();
        const uint8_t* uvPlane = yPlane + ySize;
        if (!uploadImageData(engine,
                             video.lumaImage,
                             yPlane,
                             ySize,
                             decoder.width,
                             decoder.height,
                             VK_FORMAT_R8_UNORM))
        {
            return false;
        }
        if (!uploadImageData(engine,
                             video.chromaImage,
                             uvPlane,
                             uvSize,
                             decoder.chromaWidth,
                             decoder.chromaHeight,
                             VK_FORMAT_R8G8_UNORM))
        {
            return false;
        }
    }
    else
    {
        const uint8_t* yPlane = frame.buffer.data();
        const uint8_t* uvPlane = yPlane + decoder.yPlaneBytes;
        const bool sixteenBit = decoder.bytesPerComponent > 1;
        const VkFormat lumaFormat = sixteenBit ? VK_FORMAT_R16_UNORM : VK_FORMAT_R8_UNORM;
        const VkFormat chromaFormat = sixteenBit ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R8G8_UNORM;
        if (!uploadImageData(engine,
                             video.lumaImage,
                             yPlane,
                             decoder.yPlaneBytes,
                             decoder.width,
                             decoder.height,
                             lumaFormat))
        {
            return false;
        }
        if (!uploadImageData(engine,
                             video.chromaImage,
                             uvPlane,
                             decoder.uvPlaneBytes,
                             decoder.chromaWidth,
                             decoder.chromaHeight,
                             chromaFormat))
        {
            return false;
        }
    }

    video.descriptors.width = decoder.width;
    video.descriptors.height = decoder.height;
    video.descriptors.chromaDivX = decoder.chromaDivX;
    video.descriptors.chromaDivY = decoder.chromaDivY;
    video.descriptors.luma.view = video.lumaImage.view;
    video.descriptors.luma.sampler = video.sampler;
    video.descriptors.chroma.view = video.chromaImage.view ? video.chromaImage.view : video.lumaImage.view;
    video.descriptors.chroma.sampler = video.sampler;
    return true;
}

double advancePlayback(VideoPlaybackState& state, bool playing)
{
    if (state.video.sampler == VK_NULL_HANDLE)
    {
        return 0.0;
    }

    pumpDecodedFrames(state);

    if (!playing)
    {
        return state.lastDisplayedSeconds;
    }

    if (state.pendingFrames.empty())
    {
        if (state.decoder.finished.load() && !state.decoder.threadRunning.load() &&
            isDebugEnabled(DebugCategory::Decode))
        {
            std::cout << "[Video2D] End of video reached" << std::endl;
        }
        return state.lastDisplayedSeconds;
    }

    auto currentTime = std::chrono::steady_clock::now();
    auto& nextFrame = state.pendingFrames.front();

    if (!state.playbackClockInitialized)
    {
        state.playbackClockInitialized = true;
        state.basePtsSeconds = nextFrame.ptsSeconds;
        state.lastFramePtsSeconds = nextFrame.ptsSeconds;
        state.lastFrameRenderTime = currentTime;
    }

    double frameDelta = nextFrame.ptsSeconds - state.lastFramePtsSeconds;
    if (frameDelta < 1e-6)
    {
        frameDelta = 1.0 / std::max(30.0, state.decoder.fps);
    }

    auto targetTime = state.lastFrameRenderTime + std::chrono::duration<double>(frameDelta);
    if (currentTime + std::chrono::milliseconds(1) < targetTime)
    {
        return state.lastDisplayedSeconds;
    }

    auto frame = std::move(nextFrame);
    state.pendingFrames.pop_front();

    if (!uploadDecodedFrame(state.video, state.engine, state.decoder, frame))
    {
        std::cerr << "[Video2D] Failed to upload decoded frame." << std::endl;
    }

    state.lastFramePtsSeconds = frame.ptsSeconds;
    state.lastFrameRenderTime = currentTime;
    state.lastDisplayedSeconds = std::max(0.0, state.lastFramePtsSeconds - state.basePtsSeconds);
    return state.lastDisplayedSeconds;
}

// Scrubber GPU drawing is now handled inside the video_blit compute shader; the
// standalone scrubber compute path has been removed.

bool cursorInScrubber(double x, double y, int windowWidth, int windowHeight)
{
    // Use the actual scrubber pixel size to align hit testing with the overlay quad
    const double scrubberWidth = static_cast<double>(kScrubberWidth);
    const double scrubberHeight = static_cast<double>(kScrubberHeight);
    const double margin = 20.0; // small bottom padding
    const double left = (static_cast<double>(windowWidth) - scrubberWidth) * 0.5;
    const double right = left + scrubberWidth;
    const double top = static_cast<double>(windowHeight) - scrubberHeight - margin;
    const double bottom = top + scrubberHeight;
    return x >= left && x <= right && y >= top && y <= bottom;
}
}

int main(int argc, char** argv)
{
    parseDebugFlags(argc, argv);

    Engine* engine = nullptr;
    Display2D* display = nullptr;
    try {
        engine = new Engine();
        std::cout << "[Video2D] Initializing display..." << std::endl;
        display = new Display2D(engine, 1280, 720, "Motive Video 2D");
        std::cout << "[Video2D] Display initialized successfully." << std::endl;
    } catch (const std::exception& ex) {
        std::cerr << "[Video2D] FATAL: Exception during engine or display initialization: " << ex.what() << std::endl;
        delete display;
        delete engine;
        return 1;
    }
    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(display->window, &fbWidth, &fbHeight);
    fbWidth = std::max(1, fbWidth);
    fbHeight = std::max(1, fbHeight);
    float fbWidthF = static_cast<float>(fbWidth);
    float fbHeightF = static_cast<float>(fbHeight);
    glfwSetScrollCallback(display->window, onScroll);
    //glfwSetKeyCallback(display->window, onKey);

    Light sceneLight(glm::vec3(0.0f, 0.0f, 1.0f),
                     glm::vec3(0.1f),
                     glm::vec3(0.9f));
    sceneLight.setDiffuse(glm::vec3(1.0f, 0.95f, 0.9f));
    engine->setLight(sceneLight);

    VideoPlaybackState playbackState;
    double videoDurationSeconds = 0.0;
    if (!initializeVideoPlayback(kVideoPath, engine, playbackState, videoDurationSeconds))
    {
        delete engine;
        return 1;
    }

    OverlayCompute overlayCompute{};
    if (!initializeOverlayCompute(engine, overlayCompute))
    {
        video::cleanupVideoDecoder(playbackState.decoder);
        delete engine;
        return 1;
    }

    bool playing = true;
    bool spaceHeld = false;
    bool mouseHeld = false;
    auto fpsLastSample = std::chrono::steady_clock::now();
    int fpsFrameCounter = 0;
    float currentFps = 0.0f;
    // Rectangle state
    float rectHeight = fbHeightF;
    float rectWidth = rectHeight * (9.0f / 16.0f);
    glm::vec2 rectCenter(fbWidthF * 0.5f, fbHeightF * 0.5f);

    while (true)
    {
        display->pollEvents();
        glfwGetFramebufferSize(display->window, &fbWidth, &fbHeight);
        fbWidth = std::max(1, fbWidth);
        fbHeight = std::max(1, fbHeight);
        fbWidthF = static_cast<float>(fbWidth);
        fbHeightF = static_cast<float>(fbHeight);
        float windowWidthF = static_cast<float>(std::max(1, display->width));
        float windowHeightF = static_cast<float>(std::max(1, display->height));
        float cursorScaleX = fbWidthF / windowWidthF;
        float cursorScaleY = fbHeightF / windowHeightF;
        rectHeight = std::min(rectHeight, fbHeightF);
        rectWidth = rectHeight * (9.0f / 16.0f);
        rectCenter.x = std::clamp(rectCenter.x, 0.0f, fbWidthF);
        rectCenter.y = std::clamp(rectCenter.y, 0.0f, fbHeightF);
        const uint32_t fbWidthU = static_cast<uint32_t>(fbWidth);
        const uint32_t fbHeightU = static_cast<uint32_t>(fbHeight);
        if ((playbackState.fpsOverlay.lastRefWidth != fbWidthU ||
             playbackState.fpsOverlay.lastRefHeight != fbHeightU) &&
            playbackState.fpsOverlay.lastFpsValue >= 0.0f)
        {
            updateFpsOverlay(playbackState, playbackState.fpsOverlay.lastFpsValue, fbWidthU, fbHeightU);
        }

        if (glfwGetKey(display->window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {                
            std::cout << "ESC DETECTED\n";
            glfwSetWindowShouldClose(display->window, GLFW_TRUE);
            break;
        }

        if (display->shouldClose())
        {
            break;
        }

        bool spaceDown = glfwGetKey(display->window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceDown && !spaceHeld)
        {
            playing = !playing;
        }
        spaceHeld = spaceDown;

        int mouseState = glfwGetMouseButton(display->window, GLFW_MOUSE_BUTTON_LEFT);
        if (mouseState == GLFW_PRESS && !mouseHeld)
        {
            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(display->window, &cursorX, &cursorY);
            if (cursorInScrubber(cursorX, cursorY, display->width, display->height))
            {
                playing = !playing;
            }
            else
            {
                rectCenter = glm::vec2(static_cast<float>(cursorX * cursorScaleX),
                                       static_cast<float>(cursorY * cursorScaleY));
                if (isDebugEnabled(DebugCategory::Overlay))
                {
                    std::cout << "[Video2D] Rectangle recentered to (" << rectCenter.x << ", " << rectCenter.y
                              << ") size=(" << rectWidth << " x " << rectHeight << ")\n";
                }
            }
        }
        mouseHeld = (mouseState == GLFW_PRESS);

        // Scroll to resize rectangle
        double scrollDelta = g_scrollDelta;
        g_scrollDelta = 0.0;
        if (std::abs(scrollDelta) > 0.0)
        {
            float scale = 1.0f + static_cast<float>(scrollDelta) * 0.05f;
            rectHeight = std::clamp(rectHeight * scale, 50.0f, fbHeightF);
            rectWidth = rectHeight * (9.0f / 16.0f);
        }

        double playbackSeconds = advancePlayback(playbackState, playing);
        double normalizedProgress = videoDurationSeconds > 0.0
                                        ? std::clamp(playbackSeconds / videoDurationSeconds, 0.0, 1.0)
                                        : 0.0;
        // FPS tracking
        fpsFrameCounter++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - fpsLastSample).count();
        if (elapsed >= 500)
        {
            currentFps = static_cast<float>(fpsFrameCounter) * 1000.0f / static_cast<float>(elapsed);
            fpsFrameCounter = 0;
            fpsLastSample = now;
            updateFpsOverlay(playbackState, currentFps, fbWidthU, fbHeightU);
        }

        // Draw overlay (rectangle) via GPU compute into overlay image
        runOverlayCompute(engine,
                          overlayCompute,
                          playbackState.overlay.image,
                          static_cast<uint32_t>(fbWidth),
                          static_cast<uint32_t>(fbHeight),
                          glm::vec2(rectCenter.x, rectCenter.y),
                          glm::vec2(rectWidth, rectHeight),
                          3.0f,
                          3.0f);
        playbackState.overlay.info.overlay.view = playbackState.overlay.image.view;
        playbackState.overlay.info.overlay.sampler = playbackState.overlay.sampler;
        playbackState.overlay.info.extent = {static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight)};
        playbackState.overlay.info.offset = {0, 0};
        playbackState.overlay.info.enabled = true;

        display->renderFrame(playbackState.video.descriptors,
                             playbackState.overlay.info,
                             playbackState.fpsOverlay.info,
                             playbackState.colorInfo,
                             static_cast<float>(normalizedProgress),
                             playing ? 1.0f : 0.0f);
    }

    const bool logCleanup = isDebugEnabled(DebugCategory::Cleanup);
    auto cleanupStart = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] Starting cleanup..." << std::endl;
    }
    
    auto start1 = std::chrono::steady_clock::now();
    destroyOverlayCompute(overlayCompute);
    auto end1 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyOverlayCompute took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1).count() 
                  << " ms" << std::endl;
    }
    
    auto start2 = std::chrono::steady_clock::now();
    video::stopAsyncDecoding(playbackState.decoder);
    auto end2 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] stopAsyncDecoding took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2).count() 
                  << " ms" << std::endl;
    }
    
    auto start3 = std::chrono::steady_clock::now();
    destroyImageResource(engine, playbackState.video.lumaImage);
    auto end3 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyImageResource(lumaImage) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end3 - start3).count() 
                  << " ms" << std::endl;
    }
    
    auto start4 = std::chrono::steady_clock::now();
    destroyImageResource(engine, playbackState.video.chromaImage);
    auto end4 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyImageResource(chromaImage) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end4 - start4).count() 
                  << " ms" << std::endl;
    }
    
    auto start5 = std::chrono::steady_clock::now();
    destroyImageResource(engine, playbackState.overlay.image);
    auto end5 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyImageResource(overlay.image) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end5 - start5).count() 
                  << " ms" << std::endl;
    }
    
    auto start6 = std::chrono::steady_clock::now();
    destroyImageResource(engine, playbackState.fpsOverlay.image);
    auto end6 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyImageResource(fpsOverlay.image) took "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end6 - start6).count()
                  << " ms" << std::endl;
    }
    
    auto start7 = std::chrono::steady_clock::now();
    if (playbackState.video.sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(engine->logicalDevice, playbackState.video.sampler, nullptr);
        playbackState.video.sampler = VK_NULL_HANDLE;
    }
    auto end7 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] vkDestroySampler(video) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end7 - start7).count() 
                  << " ms" << std::endl;
    }
    
    auto start8 = std::chrono::steady_clock::now();
    if (playbackState.overlay.sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(engine->logicalDevice, playbackState.overlay.sampler, nullptr);
        playbackState.overlay.sampler = VK_NULL_HANDLE;
    }
    auto end8 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] vkDestroySampler(overlay) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end8 - start8).count() 
                  << " ms" << std::endl;
    }
    
    auto start9 = std::chrono::steady_clock::now();
    video::cleanupVideoDecoder(playbackState.decoder);
    auto end9 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] cleanupVideoDecoder took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end9 - start9).count() 
                  << " ms" << std::endl;
    }
    
    auto start10 = std::chrono::steady_clock::now();
    if (display)
    {
        display->shutdown();
        delete display;
        display = nullptr;
    }
    auto end10 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] delete display took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end10 - start10).count() 
                  << " ms" << std::endl;
    }
    
    auto start11 = std::chrono::steady_clock::now();
    delete engine;
    auto end11 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] delete engine took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end11 - start11).count() 
                  << " ms" << std::endl;
    }
    
    auto cleanupEnd = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        auto totalCleanup = std::chrono::duration_cast<std::chrono::milliseconds>(cleanupEnd - cleanupStart);
        std::cout << "[Video2D] Total cleanup took " << totalCleanup.count() << " ms" << std::endl;
    }
    
    return 0;
}
