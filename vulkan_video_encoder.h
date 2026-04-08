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
// Encoder Types
// ============================================================================

enum class EncodeCodec
{
    H264,
    H265
};

struct EncodeProcs
{
    PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR getVideoFormats = nullptr;
    PFN_vkCreateVideoSessionKHR createSession = nullptr;
    PFN_vkDestroyVideoSessionKHR destroySession = nullptr;
    PFN_vkCreateVideoSessionParametersKHR createSessionParams = nullptr;
    PFN_vkDestroyVideoSessionParametersKHR destroySessionParams = nullptr;
    PFN_vkCmdEncodeVideoKHR cmdEncode = nullptr;
};

EncodeProcs& getEncodeProcs();
bool ensureEncodeProcs(Engine& engine);

struct EncodeProfile
{
    EncodeCodec codec;
    VkVideoProfileInfoKHR profile{};
    VkVideoEncodeH264ProfileInfoKHR h264{};
    VkVideoEncodeH265ProfileInfoKHR h265{};
    VkFormat encodeFormat = VK_FORMAT_UNDEFINED;
};

std::optional<EncodeProfile> selectEncodeProfile(Engine& engine, EncodeCodec codec);

// ============================================================================
// Encode Session
// ============================================================================

struct EncodeSession
{
    VkVideoSessionKHR session = VK_NULL_HANDLE;
    VkVideoSessionParametersKHR sessionParams = VK_NULL_HANDLE;
    VkVideoProfileInfoKHR profile{};
    VkFormat encodeFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D codedExtent{};
    std::vector<VkImage> dpbImages;
    std::vector<VkDeviceMemory> dpbMemory;
    std::vector<VkImageView> dpbPlane0Views; // luma
    std::vector<VkImageView> dpbPlane1Views; // chroma
    std::vector<VkImageView> dpbViews; // combined view (color) for encode usage
    VkBuffer bitstreamBuffer = VK_NULL_HANDLE;
    VkDeviceMemory bitstreamMemory = VK_NULL_HANDLE;
    VkDeviceSize bitstreamSize = 0;
    uint32_t maxDpbSlots = 0;
};

std::optional<EncodeSession> createEncodeSession(Engine& engine,
                                                 const EncodeProfile& profile,
                                                 VkExtent2D codedExtent,
                                                 uint32_t maxDpbSlots);

void destroyEncodeSession(Engine& engine, EncodeSession& session);

// ============================================================================
// Encode Pipeline Resources
// ============================================================================

struct EncodeResources
{
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkQueryPool queryPool = VK_NULL_HANDLE;
    uint32_t feedbackFlags = 0;
    uint32_t dpbSlotIndex = 0;
    VkDeviceSize lastBitstreamOffset = 0;
    VkDeviceSize lastBytesWritten = 0;
};

bool initEncodeResources(Engine& engine, EncodeSession& session, EncodeResources& outRes);

bool recordEncode(Engine& engine,
                  const EncodeSession& session,
                  EncodeResources& res,
                  VkImage srcImage,
                  VkExtent2D srcExtent,
                  VkImageLayout srcLayout,
                  uint32_t srcQueueFamilyIndex,
                  uint32_t& outSlot);

bool retrieveEncodedBitstream(Engine& engine,
                              const EncodeSession& session,
                              const EncodeResources& res,
                              std::vector<uint8_t>& outData);

void destroyEncodeResources(Engine& engine, EncodeResources& res);

} // namespace vkvideo
} // namespace motive
