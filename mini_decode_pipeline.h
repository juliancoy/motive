#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "mini_decoder_session.h"

class Engine;

struct MiniDecodeResources
{
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkBuffer bitstreamBuffer = VK_NULL_HANDLE;
    VkDeviceMemory bitstreamMemory = VK_NULL_HANDLE;
    VkDeviceSize bitstreamSize = 0;
    uint32_t dpbSlotIndex = 0;
};

// Allocate command pool/buffer and a simple bitstream buffer for decode submissions.
bool initDecodeResources(Engine& engine, MiniDecodeSession& session, MiniDecodeResources& outRes, VkDeviceSize bitstreamCapacity);

// Record a vkCmdDecodeVideoKHR for a single frame; srcData is Annex-B payload.
// Returns the DPB slot index used for this frame via outSlot.
bool recordDecode(Engine& engine,
                  const MiniDecodeSession& session,
                  MiniDecodeResources& res,
                  const uint8_t* srcData,
                  size_t srcSize,
                  VkExtent2D codedExtent,
                  uint32_t& outSlot);
