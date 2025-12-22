#include "mini_encoder.h"

#include <array>
#include <iostream>
#include "engine.h"

namespace
{
MiniEncodeProcs gMiniEncodeProcs;
}

MiniEncodeProcs& getMiniEncodeProcs()
{
    return gMiniEncodeProcs;
}

bool ensureMiniEncodeProcs(Engine& engine)
{
    auto& procs = gMiniEncodeProcs;
    if (procs.getVideoFormats && procs.createSession && procs.createSessionParams && procs.cmdEncode)
    {
        return true;
    }

    procs.getVideoFormats = reinterpret_cast<PFN_vkGetPhysicalDeviceVideoFormatPropertiesKHR>(
        vkGetInstanceProcAddr(engine.instance, "vkGetPhysicalDeviceVideoFormatPropertiesKHR"));
    procs.createSession = reinterpret_cast<PFN_vkCreateVideoSessionKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkCreateVideoSessionKHR"));
    procs.destroySession = reinterpret_cast<PFN_vkDestroyVideoSessionKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkDestroyVideoSessionKHR"));
    procs.createSessionParams = reinterpret_cast<PFN_vkCreateVideoSessionParametersKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkCreateVideoSessionParametersKHR"));
    procs.destroySessionParams = reinterpret_cast<PFN_vkDestroyVideoSessionParametersKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkDestroyVideoSessionParametersKHR"));
    procs.cmdEncode = reinterpret_cast<PFN_vkCmdEncodeVideoKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkCmdEncodeVideoKHR"));

    bool ok = procs.getVideoFormats && procs.createSession && procs.createSessionParams &&
              procs.destroySession && procs.destroySessionParams && procs.cmdEncode;
    if (!ok)
    {
        std::cerr << "[MiniEncoder] Failed to load Vulkan video encode function pointers.\n";
    }
    return ok;
}

std::optional<MiniEncodeProfile> selectEncodeProfile(Engine& engine, MiniEncodeCodec codec)
{
    if (!ensureMiniEncodeProcs(engine))
    {
        return std::nullopt;
    }

    // Create profile-specific structures
    VkVideoEncodeH264ProfileInfoKHR h264Profile{};
    VkVideoEncodeH265ProfileInfoKHR h265Profile{};
    
    VkVideoProfileInfoKHR profile{};
    profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    profile.videoCodecOperation = (codec == MiniEncodeCodec::H264) ? VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR
                                                                   : VK_VIDEO_CODEC_OPERATION_ENCODE_H265_BIT_KHR;
    profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    
    // Set up the appropriate profile chain
    if (codec == MiniEncodeCodec::H264)
    {
        h264Profile.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H264_PROFILE_INFO_KHR;
        h264Profile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
        profile.pNext = &h264Profile;
    }
    else
    {
        h265Profile.sType = VK_STRUCTURE_TYPE_VIDEO_ENCODE_H265_PROFILE_INFO_KHR;
        h265Profile.stdProfileIdc = STD_VIDEO_H265_PROFILE_IDC_MAIN;
        profile.pNext = &h265Profile;
    }

    VkVideoProfileListInfoKHR profileList{};
    profileList.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_LIST_INFO_KHR;
    profileList.profileCount = 1;
    profileList.pProfiles = &profile;

    VkPhysicalDeviceVideoFormatInfoKHR fmtInfo{};
    fmtInfo.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VIDEO_FORMAT_INFO_KHR;
    fmtInfo.pNext = &profileList;
    fmtInfo.imageUsage = VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR | VK_IMAGE_USAGE_VIDEO_ENCODE_DPB_BIT_KHR;

    uint32_t formatCount = 0;
    VkResult res = gMiniEncodeProcs.getVideoFormats(engine.physicalDevice, &fmtInfo, &formatCount, nullptr);
    if (res != VK_SUCCESS || formatCount == 0)
    {
        std::cerr << "[MiniEncoder] No video formats for encode codec.\n";
        return std::nullopt;
    }
    std::vector<VkVideoFormatPropertiesKHR> formats(formatCount);
    for (auto& f : formats) f.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
    res = gMiniEncodeProcs.getVideoFormats(engine.physicalDevice, &fmtInfo, &formatCount, formats.data());
    if (res != VK_SUCCESS || formatCount == 0)
    {
        std::cerr << "[MiniEncoder] Failed to query video format properties.\n";
        return std::nullopt;
    }

    MiniEncodeProfile out{};
    out.codec = codec;
    out.profile = profile;
    if (codec == MiniEncodeCodec::H264)
    {
        out.h264 = h264Profile;
        out.profile.pNext = &out.h264;
    }
    else
    {
        out.h265 = h265Profile;
        out.profile.pNext = &out.h265;
    }
    out.encodeFormat = formats[0].format;
    std::cout << "[MiniEncoder] Selected encode codec "
              << (codec == MiniEncodeCodec::H264 ? "H.264" : "H.265")
              << " with " << formatCount << " supported encode formats. Using format " << out.encodeFormat << "\n";
    return out;
}
