#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include "mini_encoder_session.h"

class Engine;

struct MiniEncodeResources
{
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool queryPool = VK_NULL_HANDLE;
    VkVideoEncodeFeedbackFlagsKHR feedbackFlags = 0;
    VkDeviceSize lastBitstreamOffset = 0;
    VkDeviceSize lastBytesWritten = 0;
    uint32_t dpbSlotIndex = 0;
};

// Allocate command pool/buffer and fence for encode submissions.
bool initEncodeResources(Engine& engine, MiniEncodeSession& session, MiniEncodeResources& outRes);

// Record a vkCmdEncodeVideoKHR for a single frame; srcImage is the source image (RGBA) to encode.
// Returns the DPB slot index used for this frame via outSlot.
bool recordEncode(Engine& engine,
                  const MiniEncodeSession& session,
                  MiniEncodeResources& res,
                  VkImage srcImage,
                  VkExtent2D srcExtent,
                  VkImageLayout srcLayout,
                  uint32_t srcQueueFamilyIndex,
                  uint32_t& outSlot);

// Retrieve encoded bitstream data from the bitstream buffer after encode completion.
// Maps the buffer, copies data to output vector, and unmaps.
bool retrieveEncodedBitstream(Engine& engine,
                              const MiniEncodeSession& session,
                              const MiniEncodeResources& res,
                              std::vector<uint8_t>& outData);

// Destroy encode resources (command pool, fence).
void destroyEncodeResources(Engine& engine, MiniEncodeResources& res);
