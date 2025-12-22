#include "mini_decode_pipeline.h"

#include <cstring>
#include <iostream>
#include "engine.h"

bool initDecodeResources(Engine& engine, MiniDecodeSession& session, MiniDecodeResources& outRes, VkDeviceSize bitstreamCapacity)
{
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.queueFamilyIndex = engine.getVideoDecodeQueueFamilyIndex();
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(engine.logicalDevice, &poolInfo, nullptr, &outRes.cmdPool) != VK_SUCCESS)
    {
        std::cerr << "[MiniDecoder] Failed to create command pool\n";
        return false;
    }
    VkCommandBufferAllocateInfo alloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    alloc.commandPool = outRes.cmdPool;
    alloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    alloc.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(engine.logicalDevice, &alloc, &outRes.cmd) != VK_SUCCESS)
    {
        std::cerr << "[MiniDecoder] Failed to allocate command buffer\n";
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(engine.logicalDevice, &fenceInfo, nullptr, &outRes.fence) != VK_SUCCESS)
    {
        std::cerr << "[MiniDecoder] Failed to create decode fence\n";
        return false;
    }

    // Bitstream buffer
    VkBufferCreateInfo bufInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufInfo.size = bitstreamCapacity;
    bufInfo.usage = VK_BUFFER_USAGE_VIDEO_DECODE_SRC_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(engine.logicalDevice, &bufInfo, nullptr, &outRes.bitstreamBuffer) != VK_SUCCESS)
    {
        std::cerr << "[MiniDecoder] Failed to create bitstream buffer\n";
        return false;
    }
    VkMemoryRequirements req{};
    vkGetBufferMemoryRequirements(engine.logicalDevice, outRes.bitstreamBuffer, &req);
    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = req.size;
    allocInfo.memoryTypeIndex = engine.findMemoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vkAllocateMemory(engine.logicalDevice, &allocInfo, nullptr, &outRes.bitstreamMemory) != VK_SUCCESS)
    {
        std::cerr << "[MiniDecoder] Failed to allocate bitstream memory\n";
        return false;
    }
    vkBindBufferMemory(engine.logicalDevice, outRes.bitstreamBuffer, outRes.bitstreamMemory, 0);
    outRes.bitstreamSize = bitstreamCapacity;
    outRes.dpbSlotIndex = 0;
    return true;
}

bool recordDecode(Engine& engine,
                  const MiniDecodeSession& session,
                  MiniDecodeResources& res,
                  const uint8_t* srcData,
                  size_t srcSize,
                  VkExtent2D codedExtent,
                  uint32_t& outSlot)
{
    std::cout << "[MiniDecoder] recordDecode called, srcSize=" << srcSize << "\n";
    if (!ensureMiniVideoProcs(engine))
    {
        return false;
    }
    auto& procs = getMiniVideoProcs();
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
        std::cerr << "[MiniDecoder] Missing video coding commands\n";
        return false;
    }

    if (srcSize > res.bitstreamSize)
    {
        std::cerr << "[MiniDecoder] Bitstream too large\n";
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
                std::cerr << "[MiniDecoder] Fence wait before reuse failed: " << waitRes << "\n";
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
            std::cerr << "[MiniDecoder] Fence status error: " << fenceStatus << "\n";
            return false;
        }
    }
    vkResetCommandBuffer(res.cmd, 0);

    void* mapped = nullptr;
    VkResult mapRes = vkMapMemory(engine.logicalDevice, res.bitstreamMemory, 0, srcSize, 0, &mapped);
    if (mapRes != VK_SUCCESS || mapped == nullptr)
    {
        std::cerr << "[MiniDecoder] vkMapMemory failed: " << mapRes << "\n";
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
    std::cout << "[MiniDecoder] Using DPB slot " << slot << "\n";

    VkVideoPictureResourceInfoKHR dpbResource{VK_STRUCTURE_TYPE_VIDEO_PICTURE_RESOURCE_INFO_KHR};
    dpbResource.codedOffset = {0, 0};
    dpbResource.codedExtent = {codedExtent.width, codedExtent.height};
    dpbResource.baseArrayLayer = 0;
    // Bind the full image (plane 0) for decode output; sampling uses plane views later.
    dpbResource.imageViewBinding = session.dpbPlane0Views[slot];

    VkVideoPictureResourceInfoKHR dstResource = dpbResource; // reuse DPB as output for simplicity

    // Determine codec from session profile
    bool isH265 = session.profile.videoCodecOperation == VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
    std::cout << "[MiniDecoder] Codec is " << (isH265 ? "H.265" : "H.264") << "\n";
    
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

    std::cout << "[MiniDecoder] Calling vkCmdDecodeVideoKHR...\n";
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
        std::cerr << "[MiniDecoder] Queue submit failed: " << submitRes << "\n";
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
            std::cerr << "[MiniDecoder] Fence wait timeout (" << ++waitIters << "), still waiting...\n";
            continue;
        }
        std::cerr << "[MiniDecoder] Fence wait after submit failed: " << waitRes2 << "\n";
        return false;
    }
    outSlot = slot;
    res.dpbSlotIndex = (res.dpbSlotIndex + 1) % session.dpbPlane0Views.size();
    return true;
}
