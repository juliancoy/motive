#include "mini_encoder_session.h"

#include <cstring>
#include <iostream>
#include "engine.h"
#include <vk_video/vulkan_video_codec_h264std_encode.h>
#include <vk_video/vulkan_video_codec_h265std_encode.h>

std::optional<MiniEncodeSession> createEncodeSession(Engine& engine,
                                                     const MiniEncodeProfile& prof,
                                                     VkExtent2D codedExtent,
                                                     uint32_t maxDpbSlots)
{
    if (!ensureMiniEncodeProcs(engine))
    {
        return std::nullopt;
    }
    auto& procs = getMiniEncodeProcs();

    MiniEncodeSession sess{};
    sess.profile = prof.profile;
    sess.encodeFormat = prof.encodeFormat;
    sess.codedExtent = codedExtent;
    sess.maxDpbSlots = maxDpbSlots;

    VkVideoSessionCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_VIDEO_SESSION_CREATE_INFO_KHR};
    createInfo.queueFamilyIndex = engine.getVideoEncodeQueueFamilyIndex();
    createInfo.pVideoProfile = &sess.profile;
    createInfo.maxCodedExtent = {codedExtent.width, codedExtent.height};
    createInfo.pictureFormat = sess.encodeFormat;
    createInfo.referencePictureFormat = sess.encodeFormat;
    createInfo.maxDpbSlots = maxDpbSlots;
    createInfo.maxActiveReferencePictures = maxDpbSlots;

    VkExtensionProperties stdHeader{};
    if (sess.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR)
    {
        std::strncpy(stdHeader.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE);
        stdHeader.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H264_ENCODE_SPEC_VERSION;
    }
    else if (sess.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR)
    {
        std::strncpy(stdHeader.extensionName, VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_EXTENSION_NAME, VK_MAX_EXTENSION_NAME_SIZE);
        stdHeader.specVersion = VK_STD_VULKAN_VIDEO_CODEC_H265_ENCODE_SPEC_VERSION;
    }
    createInfo.pStdHeaderVersion = (stdHeader.extensionName[0] != '\0') ? &stdHeader : nullptr;

    if (procs.createSession(engine.logicalDevice, &createInfo, nullptr, &sess.session) != VK_SUCCESS)
    {
        std::cerr << "[MiniEncoder] Failed to create video encode session\n";
        return std::nullopt;
    }

    VkVideoSessionParametersCreateInfoKHR paramsInfo{VK_STRUCTURE_TYPE_VIDEO_SESSION_PARAMETERS_CREATE_INFO_KHR};
    paramsInfo.videoSession = sess.session;
    
    // Add codec-specific session parameters create info
    VkVideoEncodeH264SessionParametersCreateInfoKHR h264ParamsInfo{};
    VkVideoEncodeH265SessionParametersCreateInfoKHR h265ParamsInfo{};
    
    if (sess.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR)
    {
        h264ParamsInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_SESSION_PARAMETERS_CREATE_INFO_KHR;
        h264ParamsInfo.maxStdSPSCount = 1;
        h264ParamsInfo.maxStdPPSCount = 1;
        paramsInfo.pNext = &h264ParamsInfo;
    }
    else if (sess.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR)
    {
        h265ParamsInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_SESSION_PARAMETERS_CREATE_INFO_KHR;
        h265ParamsInfo.maxStdVPSCount = 1;
        h265ParamsInfo.maxStdSPSCount = 1;
        h265ParamsInfo.maxStdPPSCount = 1;
        paramsInfo.pNext = &h265ParamsInfo;
    }
    
    if (procs.createSessionParams(engine.logicalDevice, &paramsInfo, nullptr, &sess.sessionParams) != VK_SUCCESS)
    {
        std::cerr << "[MiniEncoder] Failed to create encode session parameters\n";
        procs.destroySession(engine.logicalDevice, sess.session, nullptr);
        return std::nullopt;
    }

    // Allocate DPB images for encode.
    VkImageCreateInfo imgInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imgInfo.imageType = VK_IMAGE_TYPE_2D;
    imgInfo.format = sess.encodeFormat;
    imgInfo.extent = {codedExtent.width, codedExtent.height, 1};
    imgInfo.mipLevels = 1;
    imgInfo.arrayLayers = 1;
    imgInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imgInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imgInfo.usage = VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    imgInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    sess.dpbImages.resize(maxDpbSlots);
    sess.dpbMemory.resize(maxDpbSlots);
    sess.dpbPlane0Views.resize(maxDpbSlots);
    sess.dpbPlane1Views.resize(maxDpbSlots);
    sess.dpbViews.resize(maxDpbSlots);

    for (uint32_t i = 0; i < maxDpbSlots; ++i)
    {
        if (vkCreateImage(engine.logicalDevice, &imgInfo, nullptr, &sess.dpbImages[i]) != VK_SUCCESS)
        {
            std::cerr << "[MiniEncoder] Failed to create DPB image\n";
            return std::nullopt;
        }
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(engine.logicalDevice, sess.dpbImages[i], &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = engine.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(engine.logicalDevice, &alloc, nullptr, &sess.dpbMemory[i]) != VK_SUCCESS)
        {
            std::cerr << "[MiniEncoder] Failed to allocate DPB memory\n";
            return std::nullopt;
        }
        vkBindImageMemory(engine.logicalDevice, sess.dpbImages[i], sess.dpbMemory[i], 0);

        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = sess.dpbImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = sess.encodeFormat;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
        if (vkCreateImageView(engine.logicalDevice, &viewInfo, nullptr, &sess.dpbPlane0Views[i]) != VK_SUCCESS)
        {
            std::cerr << "[MiniEncoder] Failed to create DPB plane0 view\n";
            return std::nullopt;
        }
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
        if (vkCreateImageView(engine.logicalDevice, &viewInfo, nullptr, &sess.dpbPlane1Views[i]) != VK_SUCCESS)
        {
            std::cerr << "[MiniEncoder] Failed to create DPB plane1 view\n";
            return std::nullopt;
        }
        VkImageViewCreateInfo colorViewInfo = viewInfo;
        colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        if (vkCreateImageView(engine.logicalDevice, &colorViewInfo, nullptr, &sess.dpbViews[i]) != VK_SUCCESS)
        {
            std::cerr << "[MiniEncoder] Failed to create DPB color view\n";
            return std::nullopt;
        }
    }

    // Create bitstream buffer (host visible for reading encoded data)
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = 4 * 1024 * 1024; // 4 MB initial size
    bufInfo.usage = VK_BUFFER_USAGE_VIDEO_ENCODE_DST_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(engine.logicalDevice, &bufInfo, nullptr, &sess.bitstreamBuffer) != VK_SUCCESS)
    {
        std::cerr << "[MiniEncoder] Failed to create bitstream buffer\n";
        return std::nullopt;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(engine.logicalDevice, sess.bitstreamBuffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = engine.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(engine.logicalDevice, &alloc, nullptr, &sess.bitstreamMemory) != VK_SUCCESS)
    {
        std::cerr << "[MiniEncoder] Failed to allocate bitstream memory\n";
        return std::nullopt;
    }
    vkBindBufferMemory(engine.logicalDevice, sess.bitstreamBuffer, sess.bitstreamMemory, 0);
    sess.bitstreamSize = bufInfo.size;

    std::cout << "[MiniEncoder] Created encode session, params, DPB images (" << maxDpbSlots << " slots), and bitstream buffer.\n";
    return sess;
}

void destroyEncodeSession(Engine& engine, MiniEncodeSession& session)
{
    auto& procs = getMiniEncodeProcs();
    if (session.bitstreamBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine.logicalDevice, session.bitstreamBuffer, nullptr);
        session.bitstreamBuffer = VK_NULL_HANDLE;
    }
    if (session.bitstreamMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine.logicalDevice, session.bitstreamMemory, nullptr);
        session.bitstreamMemory = VK_NULL_HANDLE;
    }
    for (size_t i = 0; i < session.dpbPlane0Views.size(); ++i)
    {
        if (session.dpbPlane0Views[i] != VK_NULL_HANDLE)
            vkDestroyImageView(engine.logicalDevice, session.dpbPlane0Views[i], nullptr);
        if (session.dpbPlane1Views[i] != VK_NULL_HANDLE)
            vkDestroyImageView(engine.logicalDevice, session.dpbPlane1Views[i], nullptr);
    }
    for (size_t i = 0; i < session.dpbViews.size(); ++i)
    {
        if (session.dpbViews[i] != VK_NULL_HANDLE)
            vkDestroyImageView(engine.logicalDevice, session.dpbViews[i], nullptr);
    }
    for (size_t i = 0; i < session.dpbImages.size(); ++i)
    {
        if (session.dpbImages[i] != VK_NULL_HANDLE)
            vkDestroyImage(engine.logicalDevice, session.dpbImages[i], nullptr);
        if (session.dpbMemory[i] != VK_NULL_HANDLE)
            vkFreeMemory(engine.logicalDevice, session.dpbMemory[i], nullptr);
    }
    if (session.sessionParams != VK_NULL_HANDLE)
    {
        procs.destroySessionParams(engine.logicalDevice, session.sessionParams, nullptr);
        session.sessionParams = VK_NULL_HANDLE;
    }
    if (session.session != VK_NULL_HANDLE)
    {
        procs.destroySession(engine.logicalDevice, session.session, nullptr);
        session.session = VK_NULL_HANDLE;
    }
    session.dpbPlane0Views.clear();
    session.dpbPlane1Views.clear();
    session.dpbImages.clear();
    session.dpbMemory.clear();
    session.dpbViews.clear();
    session.bitstreamSize = 0;
    session.maxDpbSlots = 0;
}
