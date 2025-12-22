#include "mini_decoder_session.h"

#include <cstring>
#include <iostream>
#include "engine.h"
#include <vk_video/vulkan_video_codec_h264std_decode.h>
#include <vk_video/vulkan_video_codec_h265std_decode.h>

std::optional<MiniDecodeSession> createDecodeSession(Engine& engine,
                                                     const MiniDecodeProfile& prof,
                                                     VkExtent2D codedExtent,
                                                     uint32_t maxDpbSlots)
{
    if (!ensureMiniVideoProcs(engine))
    {
        return std::nullopt;
    }
    auto& procs = getMiniVideoProcs();

    MiniDecodeSession sess{};
    sess.profile = prof.profile;
    sess.decodeFormat = prof.decodeFormat;
    sess.codedExtent = codedExtent;
    sess.maxDpbSlots = maxDpbSlots;

    VkVideoSessionCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR};
    createInfo.queueFamilyIndex = engine.getVideoDecodeQueueFamilyIndex();
    createInfo.pVideoProfile = &sess.profile;
    createInfo.maxCodedExtent = {codedExtent.width, codedExtent.height};
    createInfo.pictureFormat = sess.decodeFormat;
    createInfo.referencePictureFormat = sess.decodeFormat;
    createInfo.maxDpbSlots = maxDpbSlots;
    createInfo.maxActiveReferencePictures = maxDpbSlots;

    VkExtensionProperties stdHeader{};
    if (sess.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)
    {
        std::strncpy(stdHeader.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE);
        stdHeader.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_DECODE_SPEC_VERSION;
    }
    else if (sess.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR)
    {
        std::strncpy(stdHeader.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE);
        stdHeader.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H265_DECODE_SPEC_VERSION;
    }
    createInfo.pStdHeaderVersion = (stdHeader.extensionName[0] != '\0') ? &stdHeader : nullptr;

    if (procs.createSession(engine.logicalDevice, &createInfo, nullptr, &sess.session) != VK_SUCCESS)
    {
        std::cerr << "[MiniDecoder] Failed to create video session\n";
        return std::nullopt;
    }

    VkVideoSessionParametersCreateInfoKHR paramsInfo{VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR};
    paramsInfo.videoSession = sess.session;
    
    // Add codec-specific session parameters create info
    VkVideoDecodeH264SessionParametersCreateInfoKHR h264ParamsInfo{};
    VkVideoDecodeH265SessionParametersCreateInfoKHR h265ParamsInfo{};
    
    if (sess.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR)
    {
        h264ParamsInfo.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
        h264ParamsInfo.maxStdSPSCount = 1;
        h264ParamsInfo.maxStdPPSCount = 1;
        paramsInfo.pNext = &h264ParamsInfo;
    }
    else if (sess.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR)
    {
        h265ParamsInfo.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR;
        h265ParamsInfo.maxStdVPSCount = 1;
        h265ParamsInfo.maxStdSPSCount = 1;
        h265ParamsInfo.maxStdPPSCount = 1;
        paramsInfo.pNext = &h265ParamsInfo;
    }
    
    if (procs.createSessionParams(engine.logicalDevice, &paramsInfo, nullptr, &sess.sessionParams) != VK_SUCCESS)
    {
        std::cerr << "[MiniDecoder] Failed to create session parameters\n";
        procs.destroySession(engine.logicalDevice, sess.session, nullptr);
        return std::nullopt;
    }

    // Allocate simple DPB images.
    VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = sess.decodeFormat;
    imgInfo.extent = {codedExtent.width, codedExtent.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    sess.dpbImages.resize(maxDpbSlots);
    sess.dpbMemory.resize(maxDpbSlots);
    sess.dpbPlane0Views.resize(maxDpbSlots);
    sess.dpbPlane1Views.resize(maxDpbSlots);

    for (uint32_t i = 0; i < maxDpbSlots; ++i)
    {
        if (vkCreateImage(engine.logicalDevice, &imgInfo, nullptr, &sess.dpbImages[i]) != VK_SUCCESS)
        {
            std::cerr << "[MiniDecoder] Failed to create DPB image\n";
            return std::nullopt;
        }
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(engine.logicalDevice, sess.dpbImages[i], &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = engine.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(engine.logicalDevice, &alloc, nullptr, &sess.dpbMemory[i]) != VK_SUCCESS)
        {
            std::cerr << "[MiniDecoder] Failed to allocate DPB memory\n";
            return std::nullopt;
        }
        vkBindImageMemory(engine.logicalDevice, sess.dpbImages[i], sess.dpbMemory[i], 0);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = sess.dpbImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = sess.decodeFormat;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
        if (vkCreateImageView(engine.logicalDevice, &viewInfo, nullptr, &sess.dpbPlane0Views[i]) != VK_SUCCESS)
        {
            std::cerr << "[MiniDecoder] Failed to create DPB plane0 view\n";
            return std::nullopt;
        }
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
        if (vkCreateImageView(engine.logicalDevice, &viewInfo, nullptr, &sess.dpbPlane1Views[i]) != VK_SUCCESS)
        {
            std::cerr << "[MiniDecoder] Failed to create DPB plane1 view\n";
            return std::nullopt;
        }
    }

    std::cout << "[MiniDecoder] Created session, params, and DPB images (" << maxDpbSlots << " slots).\n";
    return sess;
}
