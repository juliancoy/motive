#pragma once

#include <vulkan/vulkan.h>
#include <optional>
#include <vector>
#include "mini_encoder.h"

class Engine;

struct MiniEncodeSession
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

// Create a minimal encode session and DPB images for the given profile/extent.
std::optional<MiniEncodeSession> createEncodeSession(Engine& engine,
                                                     const MiniEncodeProfile& profile,
                                                     VkExtent2D codedExtent,
                                                     uint32_t maxDpbSlots);

// Destroy encode session and associated resources.
void destroyEncodeSession(Engine& engine, MiniEncodeSession& session);
