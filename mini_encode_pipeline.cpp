#include "mini_encode_pipeline.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include "engine.h"

bool initEncodeResources(Engine& engine, MiniEncodeSession& session, MiniEncodeResources& outRes)
{
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = engine.getVideoEncodeQueueFamilyIndex();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(engine.logicalDevice, &poolInfo, nullptr, &outRes.cmdPool) != VK_SUCCESS)
    {
        std::cerr << "[MiniEncoder] Failed to create command pool\n";
        return false;
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = outRes.cmdPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(engine.logicalDevice, &alloc, &outRes.cmd) != VK_SUCCESS)
    {
        std::cerr << "[MiniEncoder] Failed to allocate command buffer\n";
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    if (vkCreateFence(engine.logicalDevice, &fenceInfo, nullptr, &outRes.fence) != VK_SUCCESS)
    {
        std::cerr << "[MiniEncoder] Failed to create encode fence\n";
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
        std::cerr << "[MiniEncoder] Warning: failed to create encode feedback query pool; falling back to full buffer copy\n";
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
                  const MiniEncodeSession& session,
                  MiniEncodeResources& res,
                  VkImage srcImage,
                  VkExtent2D srcExtent,
                  VkImageLayout srcLayout,
                  uint32_t srcQueueFamilyIndex,
                  uint32_t& outSlot)
{
    std::cout << "[MiniEncoder] recordEncode called\n";
    if (!ensureMiniEncodeProcs(engine))
    {
        return false;
    }
    auto& procs = getMiniEncodeProcs();
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
        std::cerr << "[MiniEncoder] Missing video coding commands\n";
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
            std::cerr << "[MiniEncoder] Fence wait before reuse failed with code " << waitRes << ". This may indicate a GPU hang.\n";
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
                std::cerr << "[MiniEncoder] Warning: failed to flush bitstream buffer after clearing\n";
            }
        }
        vkUnmapMemory(engine.logicalDevice, session.bitstreamMemory);
    }

    if (session.dpbViews.empty())
    {
        std::cerr << "[MiniEncoder] No DPB views available for encode\n";
        return false;
    }
    uint32_t slot = res.dpbSlotIndex % static_cast<uint32_t>(session.dpbViews.size());
    std::cout << "[MiniEncoder] Using DPB slot " << slot << "\n";

    if (session.dpbViews[slot] == VK_NULL_HANDLE)
    {
        std::cerr << "[MiniEncoder] DPB view not available for slot " << slot << "\n";
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
    std::cout << "[MiniEncoder] Codec is " << (isH265 ? "H.265" : "H.264") << "\n";

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
            std::cerr << "[MiniEncoder] Failed to create source image view\n";
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

    std::cout << "[MiniEncoder] BEFORE vkCmdEncodeVideoKHR\n";
    procs.cmdEncode(res.cmd, &encodeInfo);
    std::cout << "[MiniEncoder] AFTER vkCmdEncodeVideoKHR\n";

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

    std::cout << "[MiniEncoder] BEFORE vkEndCommandBuffer\n";
    vkEndCommandBuffer(res.cmd);
    std::cout << "[MiniEncoder] AFTER vkEndCommandBuffer\n";

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &res.cmd;
    std::cout << "[MiniEncoder] BEFORE vkQueueSubmit\n";
    VkResult submitRes = vkQueueSubmit(engine.getVideoEncodeQueue(), 1, &submit, res.fence);
    std::cout << "[MiniEncoder] AFTER vkQueueSubmit: " << submitRes << "\n";
    if (submitRes != VK_SUCCESS)
    {
        std::cerr << "[MiniEncoder] Queue submit failed: " << submitRes << "\n";
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
            std::cerr << "[MiniEncoder] Fence wait timeout (" << ++waitIters << "), still waiting...\n";
            continue;
        }
        std::cerr << "[MiniEncoder] Fence wait after submit failed: " << waitRes2 << "\n";
        return false;
    }
    if (waitIters >= kMaxWaitIterations)
    {
        std::cerr << "[MiniEncoder] Fence wait exceeded maximum iterations, encode may have hung\n";
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
                std::cerr << "[MiniEncoder] Encode feedback query not available yet.\n";
            }
        }
        else
        {
            std::cerr << "[MiniEncoder] Failed to read encode feedback query: " << qr << "\n";
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
                              const MiniEncodeSession& session,
                              const MiniEncodeResources& res,
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
            std::cerr << "[MiniEncoder] Warning: failed to invalidate bitstream memory before mapping: " << invRes << "\n";
        }
    }
    void* mapped = nullptr;
    VkResult mapRes = vkMapMemory(engine.logicalDevice, session.bitstreamMemory, 0, session.bitstreamSize, 0, &mapped);
    if (mapRes != VK_SUCCESS || mapped == nullptr)
    {
        std::cerr << "[MiniEncoder] vkMapMemory failed: " << mapRes << "\n";
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

void destroyEncodeResources(Engine& engine, MiniEncodeResources& res)
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
