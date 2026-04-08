#pragma once

#include <vulkan/vulkan.h>
#include <optional>
#include <filesystem>
#include <vector>
#include <cstdint>

class Engine;

namespace motive {
namespace vkvideo {

// ============================================================================
// Decoder Types
// ============================================================================

enum class DecodeCodec
{
    H264,
    H265
};

struct VideoProcs
{
    PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR getVideoFormats = nullptr;
    PFN_vkCreateVideoSessionKHR createSession = nullptr;
    PFN_vkDestroyVideoSessionKHR destroySession = nullptr;
    PFN_vkCreateVideoSessionParametersKHR createSessionParams = nullptr;
    PFN_vkDestroyVideoSessionParametersKHR destroySessionParams = nullptr;
    PFN_vkCmdDecodeVideoKHR cmdDecode = nullptr;
};

VideoProcs& getVideoProcs();
bool ensureVideoProcs(Engine& engine);

struct DecodeProfile
{
    DecodeCodec codec;
    VkVideoProfileInfoKHR profile{};
    VkVideoDecodeH264ProfileInfoKHR h264{};
    VkVideoDecodeH265ProfileInfoKHR h265{};
    VkFormat decodeFormat = VK_FORMAT_UNDEFINED;
};

std::optional<DecodeProfile> selectDecodeProfile(Engine& engine, DecodeCodec codec);

// ============================================================================
// Decode Session
// ============================================================================

struct DecodeSession
{
    VkVideoSessionKHR session = VK_NULL_HANDLE;
    VkVideoSessionParametersKHR sessionParams = VK_NULL_HANDLE;
    VkVideoProfileInfoKHR profile{};
    VkFormat decodeFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D codedExtent{};
    std::vector<VkImage> dpbImages;
    std::vector<VkDeviceMemory> dpbMemory;
    std::vector<VkImageView> dpbPlane0Views; // luma
    std::vector<VkImageView> dpbPlane1Views; // chroma
    uint32_t maxDpbSlots = 0;
};

std::optional<DecodeSession> createDecodeSession(Engine& engine,
                                                 const DecodeProfile& profile,
                                                 VkExtent2D codedExtent,
                                                 uint32_t maxDpbSlots);

// ============================================================================
// Decode Pipeline Resources
// ============================================================================

struct DecodeResources
{
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkBuffer bitstreamBuffer = VK_NULL_HANDLE;
    VkDeviceMemory bitstreamMemory = VK_NULL_HANDLE;
    VkDeviceSize bitstreamSize = 0;
    uint32_t dpbSlotIndex = 0;
};

bool initDecodeResources(Engine& engine, DecodeSession& session, DecodeResources& outRes, VkDeviceSize bitstreamCapacity);

// Record a vkCmdDecodeVideoKHR for a single frame; srcData is Annex-B payload.
// Returns the DPB slot index used for this frame via outSlot.
bool recordDecode(Engine& engine,
                  const DecodeSession& session,
                  DecodeResources& res,
                  const uint8_t* srcData,
                  size_t srcSize,
                  VkExtent2D codedExtent,
                  uint32_t& outSlot);

} // namespace vkvideo
} // namespace motive
