#pragma once

#include <vulkan/vulkan.h>
#include <optional>
#include <filesystem>

class Engine;

enum class MiniEncodeCodec
{
    H264,
    H265
};

struct MiniEncodeProcs
{
    PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR getVideoFormats = nullptr;
    PFN_vkCreateVideoSessionKHR createSession = nullptr;
    PFN_vkDestroyVideoSessionKHR destroySession = nullptr;
    PFN_vkCreateVideoSessionParametersKHR createSessionParams = nullptr;
    PFN_vkDestroyVideoSessionParametersKHR destroySessionParams = nullptr;
    PFN_vkCmdEncodeVideoKHR cmdEncode = nullptr;
};

MiniEncodeProcs& getMiniEncodeProcs();
bool ensureMiniEncodeProcs(Engine& engine);

struct MiniEncodeProfile
{
    MiniEncodeCodec codec;
    VkVideoProfileInfoKHR profile{};
    VkVideoEncodeH264ProfileInfoKHR h264{};
    VkVideoEncodeH265ProfileInfoKHR h265{};
    VkFormat encodeFormat = VK_FORMAT_UNDEFINED;
};

// Helper to choose encode profile using Engine-owned Vulkan handles.
std::optional<MiniEncodeProfile> selectEncodeProfile(Engine& engine, MiniEncodeCodec codec);
