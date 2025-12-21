#include <array>
#include <bit>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <algorithm>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "decode.h"
#include "display2d.h"
#include "engine.h"
#include "grading.hpp"
#include "overlay.hpp"
#include "utils.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libavutil/opt.h>
#include <libavutil/rational.h>
}

namespace
{
const std::filesystem::path kDefaultVideoPath("P1090533_main8_hevc_fast.mkv");

struct ComputePushConstants
{
    glm::vec2 outputSize;
    glm::vec2 videoSize;
    glm::vec2 targetOrigin;
    glm::vec2 targetSize;
    glm::vec2 cropOrigin;
    glm::vec2 cropSize;
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
    uint32_t scrubberEnabled;
    uint32_t _padScrub0;
    uint32_t _padScrub1;
    uint32_t _padScrub2;
    glm::vec4 grading;
    glm::vec4 shadows;
    glm::vec4 midtones;
    glm::vec4 highlights;
};

struct QueueSelection
{
    int index = -1;
    VkQueueFlagBits flags = static_cast<VkQueueFlagBits>(0);
    VkVideoCodecOperationFlagBitsKHR videoCaps = VK_VIDEO_CODEC_OPERATION_NONE_KHR;
    uint32_t queueCount = 1;
};

QueueSelection selectEncodeQueueFamily(VkPhysicalDevice physicalDevice, uint32_t fallbackGraphicsIndex)
{
    QueueSelection selection{};

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &familyCount, nullptr);
    if (familyCount == 0)
    {
        selection.index = static_cast<int>(fallbackGraphicsIndex);
        selection.flags = static_cast<VkQueueFlagBits>(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);
        return selection;
    }

    std::vector<VkQueueFamilyProperties2> props(familyCount);
    std::vector<VkQueueFamilyVideoPropertiesKHR> videoProps(familyCount);
    for (uint32_t i = 0; i < familyCount; ++i)
    {
        props[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        props[i].pNext = &videoProps[i];
        videoProps[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
        videoProps[i].pNext = nullptr;
    }

    vkGetPhysicalDeviceQueueFamilyProperties2(physicalDevice, &familyCount, props.data());

    const VkVideoCodecOperationFlagsKHR desiredCaps =
        VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR |
        VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR
#ifdef VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR
        | VK_VIDEO_CODEC_OPERATION_ENCODE_AV1_BIT_KHR
#endif
        ;

    auto scoreFamily = [&](uint32_t i, bool strictCaps) -> int {
        const VkQueueFlags flags = props[i].queueFamilyProperties.queueFlags;
        const VkVideoCodecOperationFlagsKHR caps = videoProps[i].videoCodecOperations;
        if (!(flags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR))
        {
            return -1;
        }
        if (strictCaps && !(caps & desiredCaps))
        {
            return -1;
        }
        // Prefer families with fewer unrelated capabilities to avoid contention.
        const uint32_t capabilityScore = static_cast<uint32_t>(__builtin_popcount(static_cast<unsigned>(flags)));
        return static_cast<int>(capabilityScore);
    };

    int bestScore = std::numeric_limits<int>::max();
    int bestIndex = -1;
    bool foundStrict = false;

    for (uint32_t i = 0; i < familyCount; ++i)
    {
        int score = scoreFamily(i, /*strictCaps=*/true);
        if (score >= 0 && score < bestScore)
        {
            bestScore = score;
            bestIndex = static_cast<int>(i);
            foundStrict = true;
        }
    }

    if (!foundStrict)
    {
        bestScore = std::numeric_limits<int>::max();
        for (uint32_t i = 0; i < familyCount; ++i)
        {
            int score = scoreFamily(i, /*strictCaps=*/false);
            if (score >= 0 && score < bestScore)
            {
                bestScore = score;
                bestIndex = static_cast<int>(i);
            }
        }
    }

    if (bestIndex >= 0)
    {
        selection.index = bestIndex;
        selection.flags = static_cast<VkQueueFlagBits>(props[bestIndex].queueFamilyProperties.queueFlags);
        selection.videoCaps = static_cast<VkVideoCodecOperationFlagBitsKHR>(videoProps[bestIndex].videoCodecOperations);
        selection.queueCount = std::max<uint32_t>(1u, props[bestIndex].queueFamilyProperties.queueCount);
        return selection;
    }

    selection.index = static_cast<int>(fallbackGraphicsIndex);
    selection.flags = static_cast<VkQueueFlagBits>(props[fallbackGraphicsIndex].queueFamilyProperties.queueFlags);
    selection.videoCaps = VK_VIDEO_CODEC_OPERATION_NONE_KHR;
    selection.queueCount = std::max<uint32_t>(1u, props[fallbackGraphicsIndex].queueFamilyProperties.queueCount);
    return selection;
}

struct OffscreenBlit
{
    Engine* engine = nullptr;
    VkExtent2D extent{0, 0};
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkImage lumaImage = VK_NULL_HANDLE;
    VkDeviceMemory lumaMemory = VK_NULL_HANDLE;
    VkImageView lumaView = VK_NULL_HANDLE;
    VkImage chromaImage = VK_NULL_HANDLE;
    VkDeviceMemory chromaMemory = VK_NULL_HANDLE;
    VkImageView chromaView = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;

    VkBuffer curveUBO = VK_NULL_HANDLE;
    VkDeviceMemory curveUBOMemory = VK_NULL_HANDLE;
    void* curveUBOMapped = nullptr;
    VkDeviceSize curveUBOSize = sizeof(glm::vec4) * 64;

    bool initialize(Engine* inEngine, uint32_t width, uint32_t height, const std::array<float, kCurveLutSize>& curve)
    {
        engine = inEngine;
        extent = {width, height};
        if (!engine || width == 0 || height == 0)
        {
            return false;
        }

        auto createImage = [&](VkFormat format, VkExtent2D imgExtent, VkImageUsageFlags usage, VkImage& outImage, VkDeviceMemory& outMemory, VkImageView& outView) -> bool {
            VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            imageInfo.imageType = VK_IMAGE_TYPE_2D;
            imageInfo.format = format;
            imageInfo.extent = {imgExtent.width, imgExtent.height, 1};
            imageInfo.mipLevels = 1;
            imageInfo.arrayLayers = 1;
            imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageInfo.usage = usage;
            imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

            if (vkCreateImage(engine->logicalDevice, &imageInfo, nullptr, &outImage) != VK_SUCCESS)
            {
                return false;
            }

            VkMemoryRequirements memReq{};
            vkGetImageMemoryRequirements(engine->logicalDevice, outImage, &memReq);
            VkMemoryAllocateInfo memAlloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            memAlloc.allocationSize = memReq.size;
            memAlloc.memoryTypeIndex = engine->findMemoryType(memReq.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            if (vkAllocateMemory(engine->logicalDevice, &memAlloc, nullptr, &outMemory) != VK_SUCCESS)
            {
                vkDestroyImage(engine->logicalDevice, outImage, nullptr);
                outImage = VK_NULL_HANDLE;
                return false;
            }
            if (vkBindImageMemory(engine->logicalDevice, outImage, outMemory, 0) != VK_SUCCESS)
            {
                vkDestroyImage(engine->logicalDevice, outImage, nullptr);
                vkFreeMemory(engine->logicalDevice, outMemory, nullptr);
                outImage = VK_NULL_HANDLE;
                outMemory = VK_NULL_HANDLE;
                return false;
            }

            VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
            viewInfo.image = outImage;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = format;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            viewInfo.subresourceRange.baseMipLevel = 0;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.baseArrayLayer = 0;
            viewInfo.subresourceRange.layerCount = 1;
            if (vkCreateImageView(engine->logicalDevice, &viewInfo, nullptr, &outView) != VK_SUCCESS)
            {
                vkDestroyImage(engine->logicalDevice, outImage, nullptr);
                vkFreeMemory(engine->logicalDevice, outMemory, nullptr);
                outImage = VK_NULL_HANDLE;
                outMemory = VK_NULL_HANDLE;
                return false;
            }
            return true;
        };

        if (!createImage(VK_FORMAT_R8G8B8A8_UNORM,
                         extent,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         image,
                         memory,
                         view))
        {
            std::cerr << "[Encode] Failed to create offscreen image.\n";
            return false;
        }

        VkExtent2D chromaExtent{(width + 1) / 2, (height + 1) / 2};
        if (!createImage(VK_FORMAT_R8_UNORM,
                         extent,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         lumaImage,
                         lumaMemory,
                         lumaView))
        {
            std::cerr << "[Encode] Failed to create offscreen luma image.\n";
            return false;
        }
        if (!createImage(VK_FORMAT_R8G8_UNORM,
                         chromaExtent,
                         VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
                         chromaImage,
                         chromaMemory,
                         chromaView))
        {
            std::cerr << "[Encode] Failed to create offscreen chroma image.\n";
            return false;
        }

        std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
        bindings[0].binding = 0;
        bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        bindings[0].descriptorCount = 1;
        bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[1].binding = 1;
        bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[1].descriptorCount = 1;
        bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[2].binding = 2;
        bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[2].descriptorCount = 1;
        bindings[2].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[3].binding = 3;
        bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[3].descriptorCount = 1;
        bindings[3].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[4].binding = 4;
        bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        bindings[4].descriptorCount = 1;
        bindings[4].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        bindings[5].binding = 5;
        bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        bindings[5].descriptorCount = 1;
        bindings[5].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

        VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
        layoutInfo.pBindings = bindings.data();
        if (vkCreateDescriptorSetLayout(engine->logicalDevice, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
        {
            std::cerr << "[Encode] Failed to create blit descriptor set layout.\n";
            return false;
        }

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
            std::cerr << "[Encode] Failed to create blit pipeline layout.\n";
            return false;
        }

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
            std::cerr << "[Encode] Failed to create blit compute pipeline.\n";
            return false;
        }
        vkDestroyShaderModule(engine->logicalDevice, shaderModule, nullptr);

        engine->createBuffer(curveUBOSize,
                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                             curveUBO,
                             curveUBOMemory);
        vkMapMemory(engine->logicalDevice, curveUBOMemory, 0, curveUBOSize, 0, &curveUBOMapped);
        if (curveUBOMapped)
        {
            std::array<glm::vec4, 64> packed{};
            for (size_t i = 0; i < 64; ++i)
            {
                packed[i] = glm::vec4(curve[i * 4 + 0],
                                      curve[i * 4 + 1],
                                      curve[i * 4 + 2],
                                      curve[i * 4 + 3]);
            }
            std::memcpy(curveUBOMapped, packed.data(), curveUBOSize);
        }

        std::array<VkDescriptorPoolSize, 3> poolSizes{};
        poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        poolSizes[0].descriptorCount = 1;
        poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        poolSizes[1].descriptorCount = 4;
        poolSizes[2].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        poolSizes[2].descriptorCount = 1;

        VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
        poolInfo.pPoolSizes = poolSizes.data();
        poolInfo.maxSets = 1;
        if (vkCreateDescriptorPool(engine->logicalDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
        {
            std::cerr << "[Encode] Failed to create blit descriptor pool.\n";
            return false;
        }

        VkDescriptorSetAllocateInfo setAlloc{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        setAlloc.descriptorPool = descriptorPool;
        setAlloc.descriptorSetCount = 1;
        setAlloc.pSetLayouts = &descriptorSetLayout;
        if (vkAllocateDescriptorSets(engine->logicalDevice, &setAlloc, &descriptorSet) != VK_SUCCESS)
        {
            std::cerr << "[Encode] Failed to allocate blit descriptor set.\n";
            return false;
        }

        return true;
    }

    void shutdown()
    {
        if (!engine)
        {
            return;
        }
        if (chromaView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(engine->logicalDevice, chromaView, nullptr);
            chromaView = VK_NULL_HANDLE;
        }
        if (lumaView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(engine->logicalDevice, lumaView, nullptr);
            lumaView = VK_NULL_HANDLE;
        }
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
        if (curveUBOMapped)
        {
            vkUnmapMemory(engine->logicalDevice, curveUBOMemory);
            curveUBOMapped = nullptr;
        }
        if (curveUBO != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(engine->logicalDevice, curveUBO, nullptr);
            curveUBO = VK_NULL_HANDLE;
        }
        if (curveUBOMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(engine->logicalDevice, curveUBOMemory, nullptr);
            curveUBOMemory = VK_NULL_HANDLE;
        }
        if (view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(engine->logicalDevice, view, nullptr);
            view = VK_NULL_HANDLE;
        }
        if (image != VK_NULL_HANDLE)
        {
            vkDestroyImage(engine->logicalDevice, image, nullptr);
            image = VK_NULL_HANDLE;
        }
        if (lumaImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(engine->logicalDevice, lumaImage, nullptr);
            lumaImage = VK_NULL_HANDLE;
        }
        if (chromaImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(engine->logicalDevice, chromaImage, nullptr);
            chromaImage = VK_NULL_HANDLE;
        }
        if (memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(engine->logicalDevice, memory, nullptr);
            memory = VK_NULL_HANDLE;
        }
        if (lumaMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(engine->logicalDevice, lumaMemory, nullptr);
            lumaMemory = VK_NULL_HANDLE;
        }
        if (chromaMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(engine->logicalDevice, chromaMemory, nullptr);
            chromaMemory = VK_NULL_HANDLE;
        }
    }

    bool render(const VideoImageSet& videoImages,
                const video::VideoColorInfo& colorInfo,
                const ColorAdjustments& adjustments)
    {
        if (!engine || !computePipeline || image == VK_NULL_HANDLE || descriptorSet == VK_NULL_HANDLE)
        {
            return false;
        }

        VkCommandBuffer cmd = engine->beginSingleTimeCommands();

        VkImageSubresourceRange colorRange{};
        colorRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        colorRange.baseMipLevel = 0;
        colorRange.levelCount = 1;
        colorRange.baseArrayLayer = 0;
        colorRange.layerCount = 1;

        VkImageMemoryBarrier toGeneral{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toGeneral.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toGeneral.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toGeneral.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toGeneral.image = image;
        toGeneral.subresourceRange = colorRange;
        toGeneral.srcAccessMask = 0;
        toGeneral.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &toGeneral);

        VkDescriptorImageInfo storageInfo{};
        storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
        storageInfo.imageView = view;

        VkDescriptorImageInfo overlayInfo{};
        overlayInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        overlayInfo.imageView = videoImages.luma.view;
        overlayInfo.sampler = videoImages.luma.sampler;

        VkDescriptorImageInfo fpsInfo{};
        fpsInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        fpsInfo.imageView = videoImages.luma.view;
        fpsInfo.sampler = videoImages.luma.sampler;

        VkDescriptorImageInfo lumaInfo{};
        lumaInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        lumaInfo.imageView = videoImages.luma.view;
        lumaInfo.sampler = videoImages.luma.sampler;

        VkDescriptorImageInfo chromaInfo{};
        chromaInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        chromaInfo.imageView = videoImages.chroma.view ? videoImages.chroma.view : videoImages.luma.view;
        chromaInfo.sampler = videoImages.chroma.sampler ? videoImages.chroma.sampler : videoImages.luma.sampler;

        VkDescriptorBufferInfo curveInfo{};
        curveInfo.buffer = curveUBO;
        curveInfo.offset = 0;
        curveInfo.range = curveUBOSize;

        std::array<VkWriteDescriptorSet, 6> writes{};
        writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[0].dstSet = descriptorSet;
        writes[0].dstBinding = 0;
        writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[0].descriptorCount = 1;
        writes[0].pImageInfo = &storageInfo;

        writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[1].dstSet = descriptorSet;
        writes[1].dstBinding = 1;
        writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[1].descriptorCount = 1;
        writes[1].pImageInfo = &overlayInfo;

        writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[2].dstSet = descriptorSet;
        writes[2].dstBinding = 2;
        writes[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[2].descriptorCount = 1;
        writes[2].pImageInfo = &fpsInfo;

        writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[3].dstSet = descriptorSet;
        writes[3].dstBinding = 3;
        writes[3].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[3].descriptorCount = 1;
        writes[3].pImageInfo = &lumaInfo;

        writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[4].dstSet = descriptorSet;
        writes[4].dstBinding = 4;
        writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[4].descriptorCount = 1;
        writes[4].pImageInfo = &chromaInfo;

        writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[5].dstSet = descriptorSet;
        writes[5].dstBinding = 5;
        writes[5].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        writes[5].descriptorCount = 1;
        writes[5].pBufferInfo = &curveInfo;

        vkUpdateDescriptorSets(engine->logicalDevice, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, computePipeline);
        vkCmdBindDescriptorSets(cmd,
                                VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelineLayout,
                                0,
                                1,
                                &descriptorSet,
                                0,
                                nullptr);

        ComputePushConstants push{};
        push.outputSize = glm::vec2(static_cast<float>(extent.width), static_cast<float>(extent.height));
        push.videoSize = glm::vec2(static_cast<float>(videoImages.width), static_cast<float>(videoImages.height));
        push.targetOrigin = glm::vec2(0.0f, 0.0f);
        push.targetSize = push.videoSize;
        push.cropOrigin = glm::vec2(0.0f);
        push.cropSize = glm::vec2(1.0f);
        push.chromaDiv = glm::vec2(static_cast<float>(videoImages.chromaDivX),
                                   static_cast<float>(videoImages.chromaDivY));
        push.colorSpace = static_cast<uint32_t>(colorInfo.colorSpace);
        push.colorRange = static_cast<uint32_t>(colorInfo.colorRange);
        push.overlayEnabled = 0;
        push.fpsOverlayEnabled = 0;
        push.overlayOrigin = glm::vec2(0.0f);
        push.overlaySize = glm::vec2(0.0f);
        push.fpsOverlayOrigin = glm::vec2(0.0f);
        push.fpsOverlaySize = glm::vec2(0.0f);
        push.scrubProgress = 0.0f;
        push.scrubPlaying = 0.0f;
        push.scrubberEnabled = 0;
        push._padScrub0 = push._padScrub1 = push._padScrub2 = 0;
        push.grading = glm::vec4(adjustments.exposure, adjustments.contrast, adjustments.saturation, 0.0f);
        push.shadows = glm::vec4(adjustments.shadows, 0.0f);
        push.midtones = glm::vec4(adjustments.midtones, 0.0f);
        push.highlights = glm::vec4(adjustments.highlights, 0.0f);

        vkCmdPushConstants(cmd, pipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(ComputePushConstants), &push);
        const uint32_t groupX = (extent.width + 15) / 16;
        const uint32_t groupY = (extent.height + 15) / 16;
        vkCmdDispatch(cmd, groupX, groupY, 1);

        VkImageMemoryBarrier toReadable{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toReadable.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
        toReadable.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toReadable.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toReadable.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toReadable.image = image;
        toReadable.subresourceRange = colorRange;
        toReadable.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        toReadable.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1, &toReadable);

        engine->endSingleTimeCommands(cmd);
        return true;
    }
};

class VulkanEncoder
{
public:
    VulkanEncoder(Engine* engine,
                  const std::filesystem::path& outputPath,
                  uint32_t width,
                  uint32_t height,
                  double fps,
                  const video::VideoColorInfo& colorInfo)
        : engine(engine), width(width), height(height), fps(fps), colorInfo(colorInfo)
    {
        initFFmpeg(outputPath);
    }

    ~VulkanEncoder()
    {
        shutdown();
    }

    bool valid() const { return codecCtx && fmtCtx && stream; }

    bool encodeFromImage(VkImage srcImage, VkImageLayout srcLayout, VkExtent2D srcExtent, double ptsSeconds)
    {
        if (!valid())
        {
            return false;
        }

        AVFrame* frame = av_frame_alloc();
        if (!frame)
        {
            return false;
        }

        frame->pts = static_cast<int64_t>(ptsSeconds / av_q2d(codecCtx->time_base));
        int err = av_hwframe_get_buffer(hwFramesRef, frame, 0);
        if (err < 0)
        {
            std::cerr << "[Encode] Failed to allocate hw frame: " << err << "\n";
            av_frame_free(&frame);
            return false;
        }

        AVVkFrame* vkFrame = reinterpret_cast<AVVkFrame*>(frame->data[0]);
        VkImage dstImage = vkFrame ? vkFrame->img[0] : VK_NULL_HANDLE;
        if (dstImage == VK_NULL_HANDLE)
        {
            std::cerr << "[Encode] Vulkan frame missing image.\n";
            av_frame_free(&frame);
            return false;
        }

        VkCommandBuffer cmd = engine->beginSingleTimeCommands();

        VkImageMemoryBarrier srcBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        srcBarrier.image = srcImage;
        srcBarrier.oldLayout = srcLayout;
        srcBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        srcBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        srcBarrier.subresourceRange.baseMipLevel = 0;
        srcBarrier.subresourceRange.levelCount = 1;
        srcBarrier.subresourceRange.baseArrayLayer = 0;
        srcBarrier.subresourceRange.layerCount = 1;
        srcBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
        srcBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;

        VkImageMemoryBarrier dstBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        dstBarrier.image = dstImage;
        dstBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        dstBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        dstBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        dstBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        dstBarrier.subresourceRange.baseMipLevel = 0;
        dstBarrier.subresourceRange.levelCount = 1;
        dstBarrier.subresourceRange.baseArrayLayer = 0;
        dstBarrier.subresourceRange.layerCount = 1;
        dstBarrier.srcAccessMask = 0;
        dstBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        std::array<VkImageMemoryBarrier, 2> barriers{srcBarrier, dstBarrier};
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             static_cast<uint32_t>(barriers.size()),
                             barriers.data());

        VkImageCopy copy{};
        copy.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.srcSubresource.mipLevel = 0;
        copy.srcSubresource.baseArrayLayer = 0;
        copy.srcSubresource.layerCount = 1;
        copy.dstSubresource = copy.srcSubresource;
        copy.extent = {srcExtent.width, srcExtent.height, 1};

        vkCmdCopyImage(cmd,
                       srcImage,
                       VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dstImage,
                       VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1,
                       &copy);

        VkImageMemoryBarrier toEncode{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        toEncode.image = dstImage;
        toEncode.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toEncode.newLayout = VK_IMAGE_LAYOUT_GENERAL;
        toEncode.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toEncode.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toEncode.subresourceRange = dstBarrier.subresourceRange;
        toEncode.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toEncode.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TRANSFER_BIT,
                             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                             0,
                             0, nullptr,
                             0, nullptr,
                             1,
                             &toEncode);

        engine->endSingleTimeCommands(cmd);

        if (vkFrame)
        {
            vkFrame->layout[0] = toEncode.newLayout;
        }

        err = avcodec_send_frame(codecCtx, frame);
        if (err < 0)
        {
            std::cerr << "[Encode] send_frame failed: " << err << "\n";
            av_frame_free(&frame);
            return false;
        }
        av_frame_free(&frame);
        return drainPackets();
    }

    bool finalize()
    {
        if (!valid())
        {
            return false;
        }
        avcodec_send_frame(codecCtx, nullptr);
        bool ok = drainPackets();
        av_write_trailer(fmtCtx);
        return ok;
    }

private:
    Engine* engine = nullptr;
    uint32_t width = 0;
    uint32_t height = 0;
    double fps = 30.0;
    video::VideoColorInfo colorInfo{};

    AVBufferRef* hwDeviceRef = nullptr;
    AVBufferRef* hwFramesRef = nullptr;
    AVFormatContext* fmtCtx = nullptr;
    AVCodecContext* codecCtx = nullptr;
    AVStream* stream = nullptr;
    std::filesystem::path outputPath;

    bool drainPackets()
    {
        AVPacket* pkt = av_packet_alloc();
        if (!pkt)
        {
            std::cerr << "[Encode] Failed to allocate packet.\n";
            return false;
        }
        while (true)
        {
            int ret = avcodec_receive_packet(codecCtx, pkt);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret < 0)
            {
                std::cerr << "[Encode] receive_packet failed: " << ret << "\n";
                av_packet_free(&pkt);
                return false;
            }

            pkt->stream_index = stream->index;
            av_packet_rescale_ts(pkt, codecCtx->time_base, stream->time_base);
            ret = av_interleaved_write_frame(fmtCtx, pkt);
            av_packet_unref(pkt);
            if (ret < 0)
            {
                std::cerr << "[Encode] write_frame failed: " << ret << "\n";
                av_packet_free(&pkt);
                return false;
            }
        }
        av_packet_free(&pkt);
        return true;
    }

    void shutdown()
    {
        if (fmtCtx && fmtCtx->pb)
        {
            avio_closep(&fmtCtx->pb);
        }
        if (codecCtx)
        {
            avcodec_free_context(&codecCtx);
        }
        if (fmtCtx)
        {
            avformat_free_context(fmtCtx);
        }
        if (hwFramesRef)
        {
            av_buffer_unref(&hwFramesRef);
        }
        if (hwDeviceRef)
        {
            av_buffer_unref(&hwDeviceRef);
        }
    }

    bool initFFmpeg(const std::filesystem::path& output)
    {
        outputPath = output;

        const AVCodec* codec = avcodec_find_encoder_by_name("h264_vulkan");
        if (!codec)
        {
            codec = avcodec_find_encoder_by_name("hevc_vulkan");
        }
        if (!codec)
        {
            std::cerr << "[Encode] Vulkan encoder not found.\n";
            return false;
        }

        codecCtx = avcodec_alloc_context3(codec);
        if (!codecCtx)
        {
            std::cerr << "[Encode] Failed to allocate codec context.\n";
            return false;
        }

        codecCtx->width = static_cast<int>(width);
        codecCtx->height = static_cast<int>(height);
        codecCtx->time_base = av_inv_q(av_d2q(fps, 1000));
        codecCtx->framerate = av_d2q(fps, 1000);
        codecCtx->pix_fmt = AV_PIX_FMT_VULKAN;
        codecCtx->sw_pix_fmt = AV_PIX_FMT_RGBA;
        codecCtx->gop_size = 120;
        codecCtx->max_b_frames = 0;
        codecCtx->color_range = static_cast<AVColorRange>(colorInfo.colorRange == video::VideoColorRange::Full ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG);
        codecCtx->colorspace = static_cast<AVColorSpace>(colorInfo.colorSpace == video::VideoColorSpace::BT2020
                                                             ? AVCOL_SPC_BT2020_NCL
                                                             : (colorInfo.colorSpace == video::VideoColorSpace::BT601 ? AVCOL_SPC_SMPTE170M : AVCOL_SPC_BT709));

        if (!initVulkanHwContext())
        {
            return false;
        }
        codecCtx->hw_frames_ctx = av_buffer_ref(hwFramesRef);
        if (!codecCtx->hw_frames_ctx)
        {
            std::cerr << "[Encode] Failed to ref hw frames context.\n";
            return false;
        }

        if (avcodec_open2(codecCtx, codec, nullptr) < 0)
        {
            std::cerr << "[Encode] Failed to open encoder.\n";
            return false;
        }

        std::filesystem::path finalPath = outputPath;
        fmtCtx = nullptr;
        if (avformat_alloc_output_context2(&fmtCtx, nullptr, nullptr, finalPath.string().c_str()) < 0 || !fmtCtx)
        {
            std::cerr << "[Encode] Failed to create output context.\n";
            return false;
        }

        stream = avformat_new_stream(fmtCtx, nullptr);
        if (!stream)
        {
            std::cerr << "[Encode] Failed to create stream.\n";
            return false;
        }
        stream->time_base = codecCtx->time_base;

        if (avcodec_parameters_from_context(stream->codecpar, codecCtx) < 0)
        {
            std::cerr << "[Encode] Failed to fill codec parameters.\n";
            return false;
        }

        if (!(fmtCtx->oformat->flags & AVFMT_NOFILE))
        {
            if (avio_open(&fmtCtx->pb, finalPath.string().c_str(), AVIO_FLAG_WRITE) < 0)
            {
                std::cerr << "[Encode] Failed to open output file.\n";
                return false;
            }
        }

        if (avformat_write_header(fmtCtx, nullptr) < 0)
        {
            std::cerr << "[Encode] Failed to write header.\n";
            return false;
        }

        std::cout << "[Encode] Writing " << finalPath << " using " << codec->name << "\n";
        return true;
    }

    bool initVulkanHwContext()
    {
        hwDeviceRef = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VULKAN);
        if (!hwDeviceRef)
        {
            std::cerr << "[Encode] Failed to allocate Vulkan hwdevice.\n";
            return false;
        }
        AVHWDeviceContext* devCtx = reinterpret_cast<AVHWDeviceContext*>(hwDeviceRef->data);
        auto* vkctx = reinterpret_cast<AVVulkanDeviceContext*>(devCtx->hwctx);
        vkctx->get_proc_addr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(vkGetInstanceProcAddr);
        vkctx->inst = engine->instance;
        vkctx->phys_dev = engine->physicalDevice;
        vkctx->act_dev = engine->logicalDevice;
        QueueSelection encodeQueue = selectEncodeQueueFamily(engine->physicalDevice, engine->graphicsQueueFamilyIndex);

        auto queryQueueProps = [this](uint32_t familyIndex, VkQueueFamilyProperties& outProps) -> bool {
            uint32_t count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &count, nullptr);
            if (familyIndex >= count || count == 0)
            {
                return false;
            }
            std::vector<VkQueueFamilyProperties> props(count);
            vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &count, props.data());
            outProps = props[familyIndex];
            return true;
        };

        std::vector<AVVulkanDeviceQueueFamily> queues;
        queues.reserve(2);

        if (encodeQueue.index >= 0)
        {
            VkQueueFamilyProperties props{};
            if (!queryQueueProps(static_cast<uint32_t>(encodeQueue.index), props))
            {
                std::cerr << "[Encode] Failed to query encode queue family properties.\n";
                return false;
            }
            AVVulkanDeviceQueueFamily q{};
            q.idx = encodeQueue.index;
            q.num = static_cast<int>(std::max<uint32_t>(1u, encodeQueue.queueCount ? encodeQueue.queueCount : props.queueCount));
            q.flags = static_cast<VkQueueFlagBits>(props.queueFlags);
            q.video_caps = encodeQueue.videoCaps;
            queues.push_back(q);
        }

        // Always provide a graphics/compute capable queue for FFmpeg blit/transfer helpers.
        VkQueueFamilyProperties graphicsProps{};
        if (queryQueueProps(engine->graphicsQueueFamilyIndex, graphicsProps))
        {
            bool merged = false;
            for (auto& q : queues)
            {
                if (q.idx == static_cast<int>(engine->graphicsQueueFamilyIndex))
                {
                    q.flags = static_cast<VkQueueFlagBits>(q.flags | graphicsProps.queueFlags);
                    q.num = std::max(q.num, static_cast<int>(graphicsProps.queueCount));
                    merged = true;
                    break;
                }
            }
            if (!merged)
            {
                AVVulkanDeviceQueueFamily q{};
                q.idx = static_cast<int>(engine->graphicsQueueFamilyIndex);
                q.num = static_cast<int>(graphicsProps.queueCount);
                q.flags = static_cast<VkQueueFlagBits>(graphicsProps.queueFlags);
                q.video_caps = VK_VIDEO_CODEC_OPERATION_NONE_KHR;
                queues.push_back(q);
            }
        }

        vkctx->nb_qf = static_cast<int>(queues.size());
        for (size_t i = 0; i < queues.size(); ++i)
        {
            vkctx->qf[i] = queues[i];
        }
        vkctx->device_features = VkPhysicalDeviceFeatures2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};

        int optDevCount = 0;
        const char* const* optExts = av_vk_get_optional_device_extensions(&optDevCount);
        if (optExts && optDevCount > 0)
        {
            vkctx->enabled_dev_extensions = optExts;
            vkctx->nb_enabled_dev_extensions = optDevCount;
        }
        else
        {
            vkctx->enabled_dev_extensions = nullptr;
            vkctx->nb_enabled_dev_extensions = 0;
        }

        int err = av_hwdevice_ctx_init(hwDeviceRef);
        if (err < 0)
        {
            std::cerr << "[Encode] Failed to init Vulkan hwdevice: " << err << "\n";
            return false;
        }

        hwFramesRef = av_hwframe_ctx_alloc(hwDeviceRef);
        if (!hwFramesRef)
        {
            std::cerr << "[Encode] Failed to allocate hw frames.\n";
            return false;
        }
        AVHWFramesContext* framesCtx = reinterpret_cast<AVHWFramesContext*>(hwFramesRef->data);
        framesCtx->format = AV_PIX_FMT_VULKAN;
        framesCtx->sw_format = AV_PIX_FMT_RGBA;
        framesCtx->width = static_cast<int>(width);
        framesCtx->height = static_cast<int>(height);

        auto* vkFrames = reinterpret_cast<AVVulkanFramesContext*>(framesCtx->hwctx);
        const VkFormat* fmt = av_vkfmt_from_pixfmt(framesCtx->sw_format);
        vkFrames->format[0] = fmt ? fmt[0] : VK_FORMAT_R8G8B8A8_UNORM;
        vkFrames->tiling = VK_IMAGE_TILING_OPTIMAL;
        vkFrames->usage = static_cast<VkImageUsageFlagBits>(VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                                            VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                                            VK_IMAGE_USAGE_SAMPLED_BIT |
                                                            VK_IMAGE_USAGE_STORAGE_BIT);

        if (av_hwframe_ctx_init(hwFramesRef) < 0)
        {
            std::cerr << "[Encode] Failed to init hw frames context.\n";
            return false;
        }
        return true;
    }
};

std::filesystem::path deriveOutputPath(const std::filesystem::path& input)
{
    std::filesystem::path stem = input.stem();
    std::filesystem::path parent = input.parent_path();
    return parent / (stem.string() + "_blit.mp4");
}

ColorAdjustments gradingToAdjustments(const GradingSettings& settings)
{
    ColorAdjustments adj{};
    adj.exposure = settings.exposure;
    adj.contrast = settings.contrast;
    adj.saturation = settings.saturation;
    adj.shadows = settings.shadows;
    adj.midtones = settings.midtones;
    adj.highlights = settings.highlights;
    grading::buildCurveLut(settings, adj.curveLut);
    adj.curveEnabled = true;
    return adj;
}
} // namespace

int main(int argc, char** argv)
{
    std::filesystem::path videoPath = (argc > 1) ? std::filesystem::path(argv[1]) : kDefaultVideoPath;
    if (videoPath.empty())
    {
        videoPath = kDefaultVideoPath;
    }
    if (!std::filesystem::exists(videoPath))
    {
        std::cerr << "[Encode] Missing input file: " << videoPath << "\n";
        return 1;
    }

    Engine engine;
    VideoPlaybackState playback{};
    double durationSeconds = 0.0;
    auto cleanupPlayback = [&]() {
        video::stopAsyncDecoding(playback.decoder);
        video::cleanupVideoDecoder(playback.decoder);
        destroyExternalVideoViews(&engine, playback.video);
    };

    if (!initializeVideoPlayback(videoPath, &engine, playback, durationSeconds, std::nullopt))
    {
        std::cerr << "[Encode] Failed to initialize playback for " << videoPath << "\n";
        cleanupPlayback();
        return 1;
    }

    GradingSettings gradingSettings{};
    grading::setGradingDefaults(gradingSettings);
    grading::loadGradingSettings(std::filesystem::path("blit_settings.json"), gradingSettings);
    ColorAdjustments adjustments = gradingToAdjustments(gradingSettings);

    OffscreenBlit blit;
    if (!blit.initialize(&engine,
                         static_cast<uint32_t>(playback.decoder.width),
                         static_cast<uint32_t>(playback.decoder.height),
                         adjustments.curveLut))
    {
        std::cerr << "[Encode] Failed to create offscreen blit pipeline.\n";
        return 1;
    }

    const std::filesystem::path outputPath = deriveOutputPath(videoPath);
    VulkanEncoder encoder(&engine,
                          outputPath,
                          static_cast<uint32_t>(playback.decoder.width),
                          static_cast<uint32_t>(playback.decoder.height),
                          playback.decoder.fps,
                          playback.colorInfo);
    if (!encoder.valid())
    {
        std::cerr << "[Encode] Encoder initialization failed.\n";
        blit.shutdown();
        cleanupPlayback();
        return 1;
    }

    size_t framesEncoded = 0;
    video::DecodedFrame decoded{};
    decoded.buffer.reserve(static_cast<size_t>(playback.decoder.bufferSize));

    while (video::decodeNextFrame(playback.decoder, decoded, /*copyFrameBuffer=*/false))
    {
        if (!uploadDecodedFrame(playback.video, &engine, playback.decoder, decoded))
        {
            std::cerr << "[Encode] Failed to upload frame " << framesEncoded << "\n";
            continue;
        }

        if (!blit.render(playback.video.descriptors, playback.colorInfo, adjustments))
        {
            std::cerr << "[Encode] Failed to render frame " << framesEncoded << "\n";
            continue;
        }

        if (!encoder.encodeFromImage(blit.image, VK_IMAGE_LAYOUT_GENERAL, blit.extent, decoded.ptsSeconds))
        {
            std::cerr << "[Encode] Encode failed at frame " << framesEncoded << "\n";
            break;
        }
        decoded.buffer.clear();
        decoded.vkSurface = {};
        framesEncoded++;
    }

    encoder.finalize();
    blit.shutdown();
    cleanupPlayback();
    std::cout << "[Encode] Completed " << framesEncoded << " frames -> " << outputPath << "\n";
    return 0;
}
