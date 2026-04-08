#include "vulkan_video_decoder.h"

#include <array>
#include <cstring>
#include <iostream>
#include "engine.h"
#include <vk_video/vulkan_video_codec_h264std_decode.h>
#include <vk_video/vulkan_video_codec_h265std_decode.h>

namespace motive {
namespace vkvideo {

namespace {
    VideoProcs gVideoProcs;
}

VideoProcs& getVideoProcs()
{
    return gVideoProcs;
}

bool ensureVideoProcs(Engine& engine)
{
    auto& procs = gVideoProcs;
    if (procs.getVideoFormats && procs.createSession && procs.createSessionParams && procs.cmdDecode)
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
    procs.cmdDecode = reinterpret_cast<PFN_vkCmdDecodeVideoKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkCmdDecodeVideoKHR"));

    bool ok = procs.getVideoFormats && procs.createSession && procs.createSessionParams &&
              procs.destroySession && procs.destroySessionParams && procs.cmdDecode;
    if (!ok)
    {
        std::cerr << "[VulkanVideoDecoder] Failed to load Vulkan video function pointers.\n";
    }
    return ok;
}

std::optional<DecodeProfile> selectDecodeProfile(Engine& engine, DecodeCodec codec)
{
    if (!ensureVideoProcs(engine))
    {
        return std::nullopt;
    }

    // Create profile-specific structures
    VkVideoDecodeH264ProfileInfoKHR h264Profile{};
    VkVideoDecodeH265ProfileInfoKHR h265Profile{};
    
    VkVideoProfileInfoKHR profile{};
    profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    profile.videoCodecOperation = (codec == DecodeCodec::H264) ? VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR
                                                             : VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
    profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    
    // Set up the appropriate profile chain
    if (codec == DecodeCodec::H264)
    {
        h264Profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
        h264Profile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
        profile.pNext = &h264Profile;
    }
    else
    {
        h265Profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR;
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
    fmtInfo.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;

    uint32_t formatCount = 0;
    VkResult res = gVideoProcs.getVideoFormats(engine.physicalDevice, &fmtInfo, &formatCount, nullptr);
    if (res != VK_SUCCESS || formatCount == 0)
    {
        std::cerr << "[VulkanVideoDecoder] No video formats for codec.\n";
        return std::nullopt;
    }
    std::vector<VkVideoFormatPropertiesKHR> formats(formatCount);
    for (auto& f : formats) f.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
    res = gVideoProcs.getVideoFormats(engine.physicalDevice, &fmtInfo, &formatCount, formats.data());
    if (res != VK_SUCCESS || formatCount == 0)
    {
        std::cerr << "[VulkanVideoDecoder] Failed to query video format properties.\n";
        return std::nullopt;
    }

    DecodeProfile out{};
    out.codec = codec;
    out.profile = profile;
    if (codec == DecodeCodec::H264)
    {
        out.h264 = h264Profile;
        out.profile.pNext = &out.h264;
    }
    else
    {
        out.h265 = h265Profile;
        out.profile.pNext = &out.h265;
    }
    out.decodeFormat = formats[0].format;
    std::cout << "[VulkanVideoDecoder] Selected codec "
              << (codec == DecodeCodec::H264 ? "H.264" : "H.265")
              << " with " << formatCount << " supported decode formats. Using format " << out.decodeFormat << "\n";
    return out;
}

std::optional<DecodeSession> createDecodeSession(Engine& engine,
                                                 const DecodeProfile& prof,
                                                 VkExtent2D codedExtent,
                                                 uint32_t maxDpbSlots)
{
    if (!ensureVideoProcs(engine))
    {
        return std::nullopt;
    }
    auto& procs = getVideoProcs();

    DecodeSession sess{};
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
        std::cerr << "[VulkanVideoDecoder] Failed to create video session\n";
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
        std::cerr << "[VulkanVideoDecoder] Failed to create session parameters\n";
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
            std::cerr << "[VulkanVideoDecoder] Failed to create DPB image\n";
            return std::nullopt;
        }
        VkMemoryRequirements req{};
        vkGetImageMemoryRequirements(engine.logicalDevice, sess.dpbImages[i], &req);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        alloc.allocationSize = req.size;
        alloc.memoryTypeIndex = engine.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (vkAllocateMemory(engine.logicalDevice, &alloc, nullptr, &sess.dpbMemory[i]) != VK_SUCCESS)
        {
            std::cerr << "[VulkanVideoDecoder] Failed to allocate DPB memory\n";
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
            std::cerr << "[VulkanVideoDecoder] Failed to create DPB plane0 view\n";
            return std::nullopt;
        }
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_PLANE_1_BIT;
        if (vkCreateImageView(engine.logicalDevice, &viewInfo, nullptr, &sess.dpbPlane1Views[i]) != VK_SUCCESS)
        {
            std::cerr << "[VulkanVideoDecoder] Failed to create DPB plane1 view\n";
            return std::nullopt;
        }
    }

    std::cout << "[VulkanVideoDecoder] Created session, params, and DPB images (" << maxDpbSlots << " slots).\n";
    return sess;
}

bool initDecodeResources(Engine& engine, DecodeSession& session, DecodeResources& outRes, VkDeviceSize bitstreamCapacity)
{
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = engine.getVideoDecodeQueueFamilyIndex();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(engine.logicalDevice, &poolInfo, nullptr, &outRes.cmdPool) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoDecoder] Failed to create command pool\n";
        return false;
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = outRes.cmdPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(engine.logicalDevice, &alloc, &outRes.cmd) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoDecoder] Failed to allocate command buffer\n";
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(engine.logicalDevice, &fenceInfo, nullptr, &outRes.fence) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoDecoder] Failed to create decode fence\n";
        return false;
    }

    // Bitstream buffer
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = bitstreamCapacity;
    bufInfo.usage = VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(engine.logicalDevice, &bufInfo, nullptr, &outRes.bitstreamBuffer) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoDecoder] Failed to create bitstream buffer\n";
        return false;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(engine.logicalDevice, outRes.bitstreamBuffer, &req);
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = engine.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(engine.logicalDevice, &allocInfo, nullptr, &outRes.bitstreamMemory) != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoDecoder] Failed to allocate bitstream memory\n";
        return false;
    }
    vkBindBufferMemory(engine.logicalDevice, outRes.bitstreamBuffer, outRes.bitstreamMemory, 0);
    outRes.bitstreamSize = bitstreamCapacity;
    outRes.dpbSlotIndex = 0;
    return true;
}

bool recordDecode(Engine& engine,
                  const DecodeSession& session,
                  DecodeResources& res,
                  const uint8_t* srcData,
                  size_t srcSize,
                  VkExtent2D codedExtent,
                  uint32_t& outSlot)
{
    std::cout << "[VulkanVideoDecoder] recordDecode called, srcSize=" << srcSize << "\n";
    if (!ensureVideoProcs(engine))
    {
        return false;
    }
    auto& procs = getVideoProcs();
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
        std::cerr << "[VulkanVideoDecoder] Missing video coding commands\n";
        return false;
    }

    if (srcSize > res.bitstreamSize)
    {
        std::cerr << "[VulkanVideoDecoder] Bitstream too large\n";
        return false;
    }

    // Ensure previous submit is done before reusing the command buffer.
    if (res.fence != VK_NULL_HANDLE)
    {
        VkResult fenceStatus = vkGetFenceStatus(engine.logicalDevice, res.fence);
        if (fenceStatus == VK_SUCCESS)
        {
            // Fence is signaled, meaning previous submit completed. Wait to ensure.
            VkResult waitRes = vkWaitForFences(engine.logicalDevice, 1, &res.fence, VK_TRUE, UINT64_MAX);
            if (waitRes != VK_SUCCESS)
            {
                std::cerr << "[VulkanVideoDecoder] Fence wait before reuse failed: " << waitRes << "\n";
                return false;
            }
            vkResetFences(engine.logicalDevice, 1, &res.fence);
        }
        else if (fenceStatus == VK_NOT_READY)
        {
            // Fence not signaled, assume no previous submission (first call).
            // No need to wait.
        }
        else
        {
            std::cerr << "[VulkanVideoDecoder] Fence status error: " << fenceStatus << "\n";
            return false;
        }
    }
    vkResetCommandBuffer(res.cmd, 0);

    void* mapped = nullptr;
    VkResult mapRes = vkMapMemory(engine.logicalDevice, res.bitstreamMemory, 0, srcSize, 0, &mapped);
    if (mapRes != VK_SUCCESS || mapped == nullptr)
    {
        std::cerr << "[VulkanVideoDecoder] vkMapMemory failed: " << mapRes << "\n";
        return false;
    }
    std::memcpy(mapped, srcData, srcSize);
    vkUnmapMemory(engine.logicalDevice, res.bitstreamMemory);

    VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(res.cmd, &begin);

    VkVideoBeginCodingInfoKHR beginCoding{VK_STRUCTURE_TYPE_VIDEO_BEGIN_CODING_INFO_KHR};
    beginCoding.videoSession = session.session;
    beginCoding.videoSessionParameters = session.sessionParams;
    pfnBeginVideoCoding(res.cmd, &beginCoding);

    uint32_t slot = res.dpbSlotIndex % session.dpbPlane0Views.size();
    std::cout << "[VulkanVideoDecoder] Using DPB slot " << slot << "\n";

    VkVideoPictureResourceInfoKHR dpbResource{VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR};
    dpbResource.codedOffset = {0, 0};
    dpbResource.codedExtent = {codedExtent.width, codedExtent.height};
    dpbResource.baseArrayLayer = 0;
    // Bind the full image (plane 0) for decode output; sampling uses plane views later.
    dpbResource.imageViewBinding = session.dpbPlane0Views[slot];

    VkVideoPictureResourceInfoKHR dstResource = dpbResource; // reuse DPB as output for simplicity

    // Determine codec from session profile
    bool isH265 = session.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
    std::cout << "[VulkanVideoDecoder] Codec is " << (isH265 ? "H.265" : "H.264") << "\n";
    
    VkVideoDecodeH264PictureInfoKHR h264Info{VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PICTURE_INFO_KHR};
    VkVideoDecodeH265PictureInfoKHR h265Info{VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PICTURE_INFO_KHR};
    
    VkVideoDecodeInfoKHR decodeInfo{VK_STRUCTURE_TYPE_VIDEO_DECODE_INFO_KHR};
    decodeInfo.srcBuffer = res.bitstreamBuffer;
    decodeInfo.srcBufferOffset = 0;
    decodeInfo.srcBufferRange = srcSize;
    decodeInfo.dstPictureResource = dstResource;
    decodeInfo.pSetupReferenceSlot = nullptr;
    decodeInfo.referenceSlotCount = 0;
    decodeInfo.pReferenceSlots = nullptr;
    
    if (isH265) {
        decodeInfo.pNext = &h265Info;
    } else {
        decodeInfo.pNext = &h264Info;
    }

    std::cout << "[VulkanVideoDecoder] Calling vkCmdDecodeVideoKHR...\n";
    procs.cmdDecode(res.cmd, &decodeInfo);

    // Transition decoded image to GENERAL for downstream copy/sampling.
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.image = session.dpbImages[slot];
    barrier.oldLayout = VK_IMAGE_LAYOUT_VIDEO_DECODE_DST_KHR;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = VK_ACCESS_2_VIDEO_DECODE_WRITE_BIT_KHR;
    barrier.dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_TRANSFER_READ_BIT;
    barrier.srcStageMask = VK_PIPELINE_STAGE_2_VIDEO_DECODE_BIT_KHR;
    barrier.dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT;

    VkDependencyInfo dep{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dep.imageMemoryBarrierCount = 1;
    dep.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(res.cmd, &dep);

    VkVideoEndCodingInfoKHR endCoding{VK_STRUCTURE_TYPE_VIDEO_END_CODING_INFO_KHR};
    pfnEndVideoCoding(res.cmd, &endCoding);
    vkEndCommandBuffer(res.cmd);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &res.cmd;
    VkResult submitRes = vkQueueSubmit(engine.getVideoDecodeQueue(), 1, &submit, res.fence);
    if (submitRes != VK_SUCCESS)
    {
        std::cerr << "[VulkanVideoDecoder] Queue submit failed: " << submitRes << "\n";
        return false;
    }
    // Wait for decode completion so caller can safely consume DPB image.
    const uint64_t kTimeout = 500'000'000; // 0.5s
    uint32_t waitIters = 0;
    while (true)
    {
        VkResult waitRes2 = vkWaitForFences(engine.logicalDevice, 1, &res.fence, VK_TRUE, kTimeout);
        if (waitRes2 == VK_SUCCESS)
        {
            break;
        }
        if (waitRes2 == VK_TIMEOUT)
        {
            std::cerr << "[VulkanVideoDecoder] Fence wait timeout (" << ++waitIters << "), still waiting...\n";
            continue;
        }
        std::cerr << "[VulkanVideoDecoder] Fence wait after submit failed: " << waitRes2 << "\n";
        return false;
    }
    outSlot = slot;
    res.dpbSlotIndex = (res.dpbSlotIndex + 1) % session.dpbPlane0Views.size();
    return true;
}

} // namespace vkvideo
} // namespace motive
