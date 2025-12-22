#pragma once

#include <vulkan/vulkan.h>
#include <optional>
#include <filesystem>

class Engine;

enum class MiniCodec
{
    H264,
    H265
};

struct MiniVideoProcs
{
    PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR getVideoFormats = nullptr;
    PFN_vkCreateVideoSessionKHR createSession = nullptr;
    PFN_vkDestroyVideoSessionKHR destroySession = nullptr;
    PFN_vkCreateVideoSessionParametersKHR createSessionParams = nullptr;
    PFN_vkDestroyVideoSessionParametersKHR destroySessionParams = nullptr;
    PFN_vkCmdDecodeVideoKHR cmdDecode = nullptr;
};

MiniVideoProcs& getMiniVideoProcs();
bool ensureMiniVideoProcs(Engine& engine);

struct MiniDecodeProfile
{
    MiniCodec codec;
    VkVideoProfileInfoKHR profile{};
    VkVideoDecodeH264ProfileInfoKHR h264{};
    VkVideoDecodeH265ProfileInfoKHR h265{};
    VkFormat decodeFormat = VK_FORMAT_UNDEFINED;
};

// Helper to choose decode profile using Engine-owned Vulkan handles.
std::optional<MiniDecodeProfile> selectDecodeProfile(Engine& engine, MiniCodec codec);
