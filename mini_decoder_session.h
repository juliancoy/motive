#pragma once

#include <vulkan/vulkan.h>
#include <optional>
#include <vector>
#include "mini_decoder.h"

class Engine;

struct MiniDecodeSession
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

// Create a minimal decode session and DPB images for the given profile/extent.
std::optional<MiniDecodeSession> createDecodeSession(Engine& engine,
                                                     const MiniDecodeProfile& profile,
                                                     VkExtent2D codedExtent,
                                                     uint32_t maxDpbSlots);
