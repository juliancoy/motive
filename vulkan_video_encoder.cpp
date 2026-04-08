#include "vulkan_video_encoder.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include "engine.h"
#include <vk_video/vulkan_video_codec_h264std_encode.h>
#include <vk_video/vulkan_video_codec_h265std_encode.h>

namespace motive {
namespace vkvideo {

namespace {
    EncodeProcs gEncodeProcs;
}

EncodeProcs& getEncodeProcs()
{
    return gEncodeProcs;
}

bool ensureEncodeProcs(Engine& engine)
{
    auto& procs = gEncodeProcs;
    if (procs.getVideoFormats && procs.createSession && procs.createSessionParams && procs.cmdEncode)
    {
        return true;
    }

    procs.getVideoFormats = reinterpret_cast<PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR>(
        vkGetInstanceProcAddr(engine.instance, "vkGetPhysicalDeviceVideoFormatPropertiesKHR"));
    procs.createSession = reinterpret_cast<PFN_vkCreateVideoSessionKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkCreateVideoSessionKHR"));
    procs.destroySession = reinterpret_cast<PFN_vkDestroyVideoSessionKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkDestroyVideoSessionKHR"));
    procs.createSessionParams = reinterpret_cast<PFN_vkCreateVideoSessionParametersKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkCreateVideoSessionParametersKHR"));
    procs.destroySessionParams = reinterpret_cast<PFN_vkDestroyVideoSessionParametersKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkDestroyVideoSessionParametersKHR"));
    procs.cmdEncode = reinterpret_cast<PFN_vkCmdEncodeVideoKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkCmdEncodeVideoKHR"));

    bool ok = procs.getVideoFormats && procs.createSession && procs.createSessionParams &&
              procs.destroySession && procs.destroySessionParams && procs.cmdEncode;
    if (!ok)
    {
        std::cerr << "[VulkanVideoEncoder] Failed to load Vulkan video encode function pointers.\n";
    }
    return ok;
}

std::optional<EncodeProfile> selectEncodeProfile(Engine& engine, EncodeCodec codec)
{
    if (!ensureEncodeProcs(engine))
    {
        return std::nullopt;
    }

    // Create profile-specific structures
    VkVideoEncodeH264ProfileInfoKHR h264Profile{};
    VkVideoEncodeH265ProfileInfoKHR h265Profile{};
    
    VkVideoProfileInfoKHR profile{};
    profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    profile.videoCodecOperation = (codec == EncodeCodec::H264) ? VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR
                                                                   : VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR;
    profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    
    // Set up the appropriate profile chain
    if (codec == EncodeCodec::H264)
    {
        h264Profile.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR;
        h264Profile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
        profile.pNext = &h264Profile;
    }
    else
    {
        h265Profile.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR;
        h265Profile.stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN;
        profile.pNext = &h265Profile;
    }

    VkVideoProfileListInfoKHR profileList{};
    profileList.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
    profileList.profileCount = 1;
    profileList.pProfiles = &profile;

    VkPhysicalDeviceVideoFormatInfoKHR fmtInfo{};
    fmtInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR;
    fmtInfo.pNext = &profileList;
    fmtInfo.imageUsage = VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR;

    uint32_t formatCount = 0;
    VkResult res = gEncodeProcs.getVideoFormats(engine.physicalDevice, &fmtInfo, &formatCount, nullptr);
    if (res != VK_SUCCESS || formatCount == 0)
    {
        std::cerr << "[VulkanVideoEncoder] No video formats for encode codec.\n";
        return std::nullopt;
    }
    std::vector<VkVideoFormatPropertiesKHR> formats(formatCount);
    for (auto& f : formats) f.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
    res = gEncodeProcs.getVideoFormats(engine.physicalDevice, &fmtInfo, &formatCount, formats.data());
    if (res != VK_SUCCESS || formatCount == 0)
    {
        std::cerr << "[VulkanVideoEncoder] Failed to query video format properties.\n";
        return std::nullopt;
    }

    EncodeProfile out{};
    out.codec = codec;
    out.profile = profile;
    if (codec == EncodeCodec::H264)
    {
        out.h264 = h264Profile;
        out.profile.pNext = &out.h264;
    }
    else
    {
        out.h265 = h265Profile;
        out.profile.pNext = &out.h265;
    }
    out.encodeFormat = formats[0].format;
    std::cout << "[VulkanVideoEncoder] Selected encode codec "
              << (codec == EncodeCodec::H264 ? "H.264" : "H.265")
              << " with " << formatCount << " supported encode formats. Using format " << out.encodeFormat << "\n";
    return out;
}

std::optional<EncodeSession> createEncodeSession(Engine& engine,
                                                 const EncodeProfile& prof,
                                                 VkExtent2D codedExtent,
                                                 uint32_t maxDpbSlots)
{
    if (!ensureEncodeProcs(engine))
    {
        return std::nullopt;
    }
    auto& procs = getEncodeProcs();

    EncodeSession sess{};
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
        std::cerr << "[VulkanVideoEncoder] Failed to create video encode session\n";
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
        std::cerr << "[VulkanVideoEncoder] Failed to create encode session parameters\n";
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
            std::cerr << "[VulkanVideoEncoder] Failed to create DPB image\n";
            return std::nullopt;
        }
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(engine.logicalDevice, sess.dpbImages[i], &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = engine.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(engine.logicalDevice, &alloc, nullptr, &sess.dpbMemory[i]) != VK_SUCCESS)
        {
            std::cerr << "[VulkanVideoEncoder] Failed to allocate DPB memory\n";
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
            std::cerr << "[VulkanVideoEncoder] Failed to create DPB plane0 view\n";
            return std::nullopt;
        }
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
        if (vkCreateImageView(engine.logicalDevice, &viewInfo, nullptr, &sess.dpbPlane1Views[i]) != VK_SUCCESS)
        {
            std::cerr << "[VulkanVideoEncoder] Failed to create DPB plane1 view\n";
            return std::nullopt;
        }
        VkImageViewCreateInfo colorViewInfo = viewInfo;
        colorViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        if (vkCreateImageView(engine.logicalDevice, &colorViewInfo, nullptr, &sess.dpbViews[i]) != VK_SUCCESS)
        {
            std::cerr << "[VulkanVideoEncoder] Failed to create DPB color view\n";
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
        std::cerr << "[VulkanVideoEncoder] Failed to create bitstream buffer\n";
        return std::nullopt;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(engine.logicalDevice, sess.bitstreamBuffer, &req);
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    alloc.allocationSize = req.size;
    alloc.memoryTypeIndex = engine.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(engine.logicalDevice, &alloc, nullptr, &sess.bitstreamMemory) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoEncoder] Failed to allocate bitstream memory\n";
        return std::nullopt;
    }
    vkBindBufferMemory(engine.logicalDevice, sess.bitstreamBuffer, sess.bitstreamMemory, 0);
    sess.bitstreamSize = bufInfo.size;

    std::cout << "[VulkanVideoEncoder] Created encode session, params, DPB images (" << maxDpbSlots << " slots), and bitstream buffer.\n";
    return sess;
}

void destroyEncodeSession(Engine& engine, EncodeSession& session)
{
    auto& procs = getEncodeProcs();
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

bool initEncodeResources(Engine& engine, EncodeSession& session, EncodeResources& outRes)
{
    (void)session;
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = engine.getVideoEncodeQueueFamilyIndex();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(engine.logicalDevice, &poolInfo, nullptr, &outRes.cmdPool) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoEncoder] Failed to create command pool\n";
        return false;
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = outRes.cmdPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(engine.logicalDevice, &alloc, &outRes.cmd) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoEncoder] Failed to allocate command buffer\n";
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(engine.logicalDevice, &fenceInfo, nullptr, &outRes.fence) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoEncoder] Failed to create encode fence\n";
        return false;
    }

    // Query pool for encode feedback (bytes written + offset).
    VkQueryPoolVideoEncodeFeedbackCreateInfoKHR feedbackInfo{VK_STRUCTURE_TYPE_QUERY_POOL_VIDEO_ENCODE_FEEDBACK_CREATE_INFO_KHR};
    feedbackInfo.encodeFeedbackFlags = VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BUFFER_OFFSET_BIT_KHR |
                                       VK_VIDEO_ENCODE_FEEDBACK_BITSTREAM_BYTES_WRITTEN_BIT_KHR;
    feedbackInfo.pNext = &session.profile;

    VkQueryPoolCreateInfo queryInfo{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    queryInfo.pNext = &feedbackInfo;
    queryInfo.queryType = VK_QUERY_TYPE_VIDEO_ENCODE_FEEDBACK_KHR;
    queryInfo.queryCount = 1;
    if (vkCreateQueryPool(engine.logicalDevice, &queryInfo, nullptr, &outRes.queryPool) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoEncoder] Warning: failed to create encode feedback query pool; falling back to full buffer copy\n";
        outRes.queryPool = VK_NULL_HANDLE;
        outRes.feedbackFlags = 0;
    }
    else
    {
        outRes.feedbackFlags = feedbackInfo.encodeFeedbackFlags;
    }

    outRes.dpbSlotIndex = 0;
    return true;
}

bool recordEncode(Engine& engine,
                  const EncodeSession& session,
                  EncodeResources& res,
                  VkImage srcImage,
                  VkExtent2D srcExtent,
                  VkImageLayout srcLayout,
                  uint32_t srcQueueFamilyIndex,
                  uint32_t& outSlot)
{
    std::cout << "[VulkanVideoEncoder] recordEncode called\n";
    if (!ensureEncodeProcs(engine))
    {
        return false;
    }
    auto& procs = getEncodeProcs();
    static PFN_vkCmdBeginVideoCodingKHR pfnBeginVideoCoding = nullptr;
    static PFN_vkCmdEndVideoCodingKHR pfnEndVideoCoding = nullptr;
    if (!pfnBeginVideoCoding || !pfnEndVideoCoding)
    {
        pfnBeginVideoCoding = reinterpret_cast<PFN_vkCmdBeginVideoCodingKHR>(
            vkGetDeviceProcAddr(engine.logicalDevice, "vkCmdBeginVideoCodingKHR"));
        pfnEndVideoCoding = reinterpret_cast<PFN_vkCmdEndVideoCodingKHR>(
            vkGetDeviceProcAddr(engine.logicalDevice, "vkCmdEndVideoCodingKHR"));
    }
    if (!pfnBeginVideoCoding || !pfnEndVideoCoding)
    {
        std::cerr << "[VulkanVideoEncoder] Missing video coding commands\n";
        return false;
    }

    // Ensure previous submit is done before reusing the command buffer.
    if (res.fence != VK_NULL_HANDLE)
    {
        // Wait indefinitely for the previous submission of this command buffer to complete.
        const uint64_t kFenceTimeout = 10'000'000'000; // 10 seconds
        VkResult waitRes = vkWaitForFences(engine.logicalDevice, 1, &res.fence, VK_TRUE, kFenceTimeout);
        if (waitRes != VK_SUCCESS)
        {
            std::cerr << "[VulkanVideoEncoder] Fence wait before reuse failed with code " << waitRes << ". This may indicate a GPU hang.\n";
            return false;
        }
        vkResetFences(engine.logicalDevice, 1, &res.fence);
    }
    vkResetCommandBuffer(res.cmd, 0);

    // Clear bitstream buffer before encoding so we can detect if nothing was written.
    void* zeroPtr = nullptr;
    if (vkMapMemory(engine.logicalDevice, session.bitstreamMemory, 0, session.bitstreamSize, 0, &zeroPtr) == VK_SUCCESS && zeroPtr)
    {
        std::memset(zeroPtr, 0, static_cast<size_t>(session.bitstreamSize));
        if (session.bitstreamSize > 0)
        {
            VkMappedMemoryRange flushRange{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
            flushRange.memory = session.bitstreamMemory;
            flushRange.offset = 0;
            flushRange.size = session.bitstreamSize;
            if (vkFlushMappedMemoryRanges(engine.logicalDevice, 1, &flushRange) != VK_SUCCESS)
            {
                std::cerr << "[VulkanVideoEncoder] Warning: failed to flush bitstream buffer after clearing\n";
            }
        }
        vkUnmapMemory(engine.logicalDevice, session.bitstreamMemory);
    }

    if (session.dpbViews.empty())
    {
        std::cerr << "[VulkanVideoEncoder] No DPB views available for encode\n";
        return false;
    }
    uint32_t slot = res.dpbSlotIndex % static_cast<uint32_t>(session.dpbViews.size());
    std::cout << "[VulkanVideoEncoder] Using DPB slot " << slot << "\n";

    if (session.dpbViews[slot] == VK_NULL_HANDLE)
    {
        std::cerr << "[VulkanVideoEncoder] DPB view not available for slot " << slot << "\n";
        return false;
    }

    VkVideoPictureResourceInfoKHR dpbResource{VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR};
    dpbResource.imageViewBinding = session.dpbViews[slot];
    dpbResource.codedOffset = {0, 0};
    dpbResource.codedExtent = {srcExtent.width, srcExtent.height};
    dpbResource.baseArrayLayer = 0;
    dpbResource.pNext = nullptr;

    VkVideoReferenceSlotInfoKHR activeSlot{VK_STRUCTURE_TYPE_VIDEO_REFERENCE_SLOT_INFO_KHR};
    activeSlot.slotIndex = slot;
    activeSlot.pPictureResource = &dpbResource;
    activeSlot.pNext = nullptr;

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(res.cmd, &begin);

    if (res.queryPool != VK_NULL_HANDLE)
    {
        vkCmdResetQueryPool(res.cmd, res.queryPool, 0, 1);
    }

    VkVideoBeginCodingInfoKHR beginCoding{VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR};
    beginCoding.videoSession = session.session;
    beginCoding.videoSessionParameters = session.sessionParams;
    beginCoding.referenceSlotCount = 1;
    beginCoding.pReferenceSlots = &activeSlot;
    pfnBeginVideoCoding(res.cmd, &beginCoding);

    // Transition srcImage to VIDEO_ENCODE_SRC layout
    VkImageMemoryBarrier2 srcBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    srcBarrier.image = srcImage;
    srcBarrier.oldLayout = srcLayout;
    srcBarrier.newLayout = VK_IMAGE_LAYOUT_VIDEO_ENCODE_SRC_KHR;
    uint32_t encodeQueueFamily = engine.getVideoEncodeQueueFamilyIndex();
    bool needOwnershipTransfer = (srcQueueFamilyIndex != VK_QUEUE_FAMILY_IGNORED &&
                                  srcQueueFamilyIndex != encodeQueueFamily);
    srcBarrier.srcQueueFamilyIndex = needOwnershipTransfer ? srcQueueFamilyIndex : VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.dstQueueFamilyIndex = needOwnershipTransfer ? encodeQueueFamily : VK_QUEUE_FAMILY_IGNORED;
    srcBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    srcBarrier.subresourceRange.baseMipLevel = 0;
    srcBarrier.subresourceRange.levelCount = 1;
    srcBarrier.subresourceRange.baseArrayLayer = 0;
    srcBarrier.subresourceRange.layerCount = 1;
    srcBarrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_SHADER_WRITE_BIT |
                               VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
    srcBarrier.dstAccessMask = VK_ACCESS_2_VIDEO_ENCODE_READ_BIT_KHR;
    srcBarrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT |
                              VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
    srcBarrier.dstStageMask = VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &srcBarrier;
    vkCmdPipelineBarrier2(res.cmd, &dep);

    // Determine codec from session profile
    bool isH265 = session.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR;
    std::cout << "[VulkanVideoEncoder] Codec is " << (isH265 ? "H.265" : "H.264") << "\n";

    VkVideoEncodeH264PictureInfoKHR h264Info{};
    VkVideoEncodeH265PictureInfoKHR h265Info{};
    VkVideoInlineQueryInfoKHR inlineQuery{VK_STRUCTURE_TYPE_VIDEO_INLINE_QUERY_INFO_KHR};

    // Picture info structures for H.265
    static StdVideoEncodeH265PictureInfo stdH265PicInfo{};
    static StdVideoEncodeH265SliceSegmentHeader stdH265SliceHeader{};
    static VkVideoEncodeH265NaluSliceSegmentInfoKHR h265SliceInfo{};
    static StdVideoEncodeH265ReferenceListsInfo stdH265RefLists{};

    // Initialize picture info structures
    if (isH265)
    {
        // For simplicity, encode all frames as I-frames.
        // A real encoder would track frame types (I, P, B).
        stdH265PicInfo = {};
        stdH265PicInfo.flags.is_reference = 0;
        stdH265PicInfo.flags.IrapPicFlag = 1;
        stdH265PicInfo.flags.pic_output_flag = 1;
        stdH265PicInfo.flags.no_output_of_prior_pics_flag = 1;
        stdH265PicInfo.pic_type = STD_VIDEO_H265_PICTURE_TYPE_IDR;
        stdH265PicInfo.sps_video_parameter_set_id = 0;
        stdH265PicInfo.pps_seq_parameter_set_id = 0;
        stdH265PicInfo.pps_pic_parameter_set_id = 0;
        stdH265PicInfo.PicOrderCntVal = res.dpbSlotIndex; // Use dpbSlotIndex as a POC
        stdH265PicInfo.TemporalId = 0;

        stdH265SliceHeader = {};
        stdH265SliceHeader.flags.first_slice_segment_in_pic_flag = 1;
        stdH265SliceHeader.slice_type = STD_VIDEO_H265_SLICE_TYPE_I;

        stdH265RefLists = {};
        h265SliceInfo = {};
        h265SliceInfo.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_NALU_SLICE_SEGMENT_INFO_KHR;
        h265SliceInfo.pStdSliceSegmentHeader = &stdH265SliceHeader;

        h265Info.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PICTURE_INFO_KHR;
        h265Info.naluSliceSegmentEntryCount = 1;
        h265Info.pNaluSliceSegmentEntries = &h265SliceInfo;
        stdH265PicInfo.pRefLists = &stdH265RefLists;
        h265Info.pStdPictureInfo = &stdH265PicInfo;
    }
    else
    {
        h264Info.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PICTURE_INFO_KHR;
        h264Info.naluSliceEntryCount = 0;
        h264Info.pNaluSliceEntries = nullptr;
        h264Info.pStdPictureInfo = nullptr;
        h264Info.generatePrefixNalu = VK_FALSE;
    }

    VkImageView srcImageView = VK_NULL_HANDLE;
    bool ownsSrcImageView = false;
    for (size_t i = 0; i < session.dpbImages.size(); ++i)
    {
        if (session.dpbImages[i] == srcImage)
        {
            srcImageView = session.dpbViews[i];
            break;
        }
    }
    if (srcImageView == VK_NULL_HANDLE)
    {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = srcImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = session.encodeFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        if (vkCreateImageView(engine.logicalDevice, &viewInfo, nullptr, &srcImageView) != VK_SUCCESS)
        {
            std::cerr << "[VulkanVideoEncoder] Failed to create source image view\n";
            return false;
        }
        ownsSrcImageView = true;
    }

    VkVideoEncodeInfoKHR encodeInfo{VK_STRUCTURE_TYPE_VIDEO_ENCODE_INFO_KHR};
    encodeInfo.srcPictureResource.imageViewBinding = srcImageView;
    encodeInfo.srcPictureResource.codedOffset = {0, 0};
    encodeInfo.srcPictureResource.codedExtent = {srcExtent.width, srcExtent.height};
    encodeInfo.srcPictureResource.baseArrayLayer = 0;
    encodeInfo.dstBuffer = session.bitstreamBuffer;
    encodeInfo.dstBufferOffset = 0;
    encodeInfo.dstBufferRange = session.bitstreamSize;
    encodeInfo.pSetupReferenceSlot = nullptr;
    encodeInfo.referenceSlotCount = 0;
    encodeInfo.pReferenceSlots = nullptr;

    // Set picture info (zero-initialized for now) and optional inline query for feedback.
    if (isH265)
    {
        inlineQuery.pNext = &h265Info;
    }
    else
    {
        inlineQuery.pNext = &h264Info;
    }
    if (res.queryPool != VK_NULL_HANDLE)
    {
        inlineQuery.queryPool = res.queryPool;
        inlineQuery.firstQuery = 0;
        inlineQuery.queryCount = 1;
        encodeInfo.pNext = &inlineQuery;
    }
    else
    {
        encodeInfo.pNext = inlineQuery.pNext;
    }

    encodeInfo.pSetupReferenceSlot = &activeSlot;

    std::cout << "[VulkanVideoEncoder] BEFORE vkCmdEncodeVideoKHR\n";
    procs.cmdEncode(res.cmd, &encodeInfo);
    std::cout << "[VulkanVideoEncoder] AFTER vkCmdEncodeVideoKHR\n";

    // Transition bitstream buffer for host reading
    VkBufferMemoryBarrier2 bufBarrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
    bufBarrier.buffer = session.bitstreamBuffer;
    bufBarrier.offset = 0;
    bufBarrier.size = session.bitstreamSize;
    bufBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    bufBarrier.srcAccessMask = VK_ACCESS_2_VIDEO_ENCODE_WRITE_BIT_KHR;
    bufBarrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
    bufBarrier.srcStageMask = VK_PIPELINE_STAGE_2_VIDEO_ENCODE_BIT_KHR;
    bufBarrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;

    VkDependencyInfo dep2{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep2.bufferMemoryBarrierCount = 1;
    dep2.pBufferMemoryBarriers = &bufBarrier;
    vkCmdPipelineBarrier2(res.cmd, &dep2);

    VkVideoEndCodingInfoKHR endCoding{VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR};
    pfnEndVideoCoding(res.cmd, &endCoding);

    std::cout << "[VulkanVideoEncoder] BEFORE vkEndCommandBuffer\n";
    vkEndCommandBuffer(res.cmd);
    std::cout << "[VulkanVideoEncoder] AFTER vkEndCommandBuffer\n";

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &res.cmd;
    std::cout << "[VulkanVideoEncoder] BEFORE vkQueueSubmit\n";
    VkResult submitRes = vkQueueSubmit(engine.getVideoEncodeQueue(), 1, &submit, res.fence);
    std::cout << "[VulkanVideoEncoder] AFTER vkQueueSubmit: " << submitRes << "\n";
    if (submitRes != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoEncoder] Queue submit failed: " << submitRes << "\n";
        return false;
    }
    // Wait for encode completion so caller can safely consume bitstream.
    const uint64_t kTimeout = 500'000'000; // 0.5s
    const uint32_t kMaxWaitIterations = 10; // Maximum 5 seconds total
    uint32_t waitIters = 0;
    while (waitIters < kMaxWaitIterations)
    {
        VkResult waitRes2 = vkWaitForFences(engine.logicalDevice, 1, &res.fence, VK_TRUE, kTimeout);
        if (waitRes2 == VK_SUCCESS)
        {
            break;
        }
        if (waitRes2 == VK_TIMEOUT)
        {
            std::cerr << "[VulkanVideoEncoder] Fence wait timeout (" << ++waitIters << "), still waiting...\n";
            continue;
        }
        std::cerr << "[VulkanVideoEncoder] Fence wait after submit failed: " << waitRes2 << "\n";
        return false;
    }
    if (waitIters >= kMaxWaitIterations)
    {
        std::cerr << "[VulkanVideoEncoder] Fence wait exceeded maximum iterations, encode may have hung\n";
        return false;
    }
    // Capture encode feedback query if available.
    res.lastBitstreamOffset = 0;
    res.lastBytesWritten = 0;
    if (res.queryPool != VK_NULL_HANDLE)
    {
        std::array<uint64_t, 3> queryData{};
        VkResult qr = vkGetQueryPoolResults(engine.logicalDevice,
                                            res.queryPool,
                                            0,
                                            1,
                                            sizeof(queryData),
                                            queryData.data(),
                                            sizeof(uint64_t),
                                            VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT | VK_QUERY_RESULT_WAIT_BIT);
        if (qr == VK_SUCCESS)
        {
            // Layout: [offset, bytesWritten, availability]
            res.lastBitstreamOffset = queryData[0];
            res.lastBytesWritten = queryData[1];
            if ((queryData[2] & 1u) == 0)
            {
                std::cerr << "[VulkanVideoEncoder] Encode feedback query not available yet.\n";
            }
        }
        else
        {
            std::cerr << "[VulkanVideoEncoder] Failed to read encode feedback query: " << qr << "\n";
        }
    }
    // Destroy the temporary image view
    if (ownsSrcImageView && srcImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine.logicalDevice, srcImageView, nullptr);
    }
    outSlot = slot;
    res.dpbSlotIndex = (res.dpbSlotIndex + 1) % session.dpbViews.size();
    return true;
}

bool retrieveEncodedBitstream(Engine& engine,
                              const EncodeSession& session,
                              const EncodeResources& res,
                              std::vector<uint8_t>& outData)
{
    if (session.bitstreamSize > 0)
    {
        VkMappedMemoryRange invalidateRange{VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};
        invalidateRange.memory = session.bitstreamMemory;
        invalidateRange.offset = 0;
        invalidateRange.size = session.bitstreamSize;
        VkResult invRes = vkInvalidateMappedMemoryRanges(engine.logicalDevice, 1, &invalidateRange);
        if (invRes != VK_SUCCESS)
        {
            std::cerr << "[VulkanVideoEncoder] Warning: failed to invalidate bitstream memory before mapping: " << invRes << "\n";
        }
    }
    void* mapped = nullptr;
    VkResult mapRes = vkMapMemory(engine.logicalDevice, session.bitstreamMemory, 0, session.bitstreamSize, 0, &mapped);
    if (mapRes != VK_SUCCESS || mapped == nullptr)
    {
        std::cerr << "[VulkanVideoEncoder] vkMapMemory failed: " << mapRes << "\n";
        return false;
    }
    const uint8_t* base = static_cast<uint8_t*>(mapped);
    VkDeviceSize copyOffset = std::min(res.lastBitstreamOffset, session.bitstreamSize);
    VkDeviceSize copySize = (res.lastBytesWritten > 0) ? res.lastBytesWritten : (session.bitstreamSize - copyOffset);
    if (copyOffset + copySize > session.bitstreamSize)
    {
        copySize = session.bitstreamSize - copyOffset;
    }
    // Fallback: if no bytes reported, trim trailing zeros.
    if (res.lastBytesWritten == 0)
    {
        VkDeviceSize lastNonZero = 0;
        for (VkDeviceSize i = 0; i < session.bitstreamSize; ++i)
        {
            if (base[i] != 0)
            {
                lastNonZero = i + 1;
            }
        }
        copyOffset = 0;
        copySize = lastNonZero;
    }
    outData.assign(base + copyOffset, base + copyOffset + copySize);
    vkUnmapMemory(engine.logicalDevice, session.bitstreamMemory);
    return true;
}

void destroyEncodeResources(Engine& engine, EncodeResources& res)
{
    if (res.queryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(engine.logicalDevice, res.queryPool, nullptr);
        res.queryPool = VK_NULL_HANDLE;
    }
    if (res.fence != VK_NULL_HANDLE)
    {
        vkDestroyFence(engine.logicalDevice, res.fence, nullptr);
        res.fence = VK_NULL_HANDLE;
    }
    if (res.cmd != VK_NULL_HANDLE)
    {
        vkFreeCommandBuffers(engine.logicalDevice, res.cmdPool, 1, &res.cmd);
        res.cmd = VK_NULL_HANDLE;
    }
    if (res.cmdPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(engine.logicalDevice, res.cmdPool, nullptr);
        res.cmdPool = VK_NULL_HANDLE;
    }
    res.dpbSlotIndex = 0;
}

} // namespace vkvideo
} // namespace motive
