#include "mini_decoder.h"

#include <array>
#include <iostream>
#include "engine.h"

namespace
{
MiniVideoProcs gMiniVideoProcs;
}

MiniVideoProcs& getMiniVideoProcs()
{
    return gMiniVideoProcs;
}

bool ensureMiniVideoProcs(Engine& engine)
{
    auto& procs = gMiniVideoProcs;
    if (procs.getVideoFormats && procs.createSession && procs.createSessionParams && procs.cmdDecode)
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
    procs.cmdDecode = reinterpret_cast<PFN_vkCmdDecodeVideoKHR>(
        vkGetDeviceProcAddr(engine.logicalDevice, "vkCmdDecodeVideoKHR"));

    bool ok = procs.getVideoFormats && procs.createSession && procs.createSessionParams &&
              procs.destroySession && procs.destroySessionParams && procs.cmdDecode;
    if (!ok)
    {
        std::cerr << "[MiniDecoder] Failed to load Vulkan video function pointers.\n";
    }
    return ok;
}

std::optional<MiniDecodeProfile> selectDecodeProfile(Engine& engine, MiniCodec codec)
{
    if (!ensureMiniVideoProcs(engine))
    {
        return std::nullopt;
    }

    // Create profile-specific structures
    VkVideoDecodeH264ProfileInfoKHR h264Profile{};
    VkVideoDecodeH265ProfileInfoKHR h265Profile{};
    
    VkVideoProfileInfoKHR profile{};
    profile.sType = VK_STRUCTURE_TYPE_VIDEO_PROFILE_INFO_KHR;
    profile.videoCodecOperation = (codec == MiniCodec::H264) ? VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR
                                                             : VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR;
    profile.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
    profile.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    profile.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
    
    // Set up the appropriate profile chain
    if (codec == MiniCodec::H264)
    {
        h264Profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H264_PROFILE_INFO_KHR;
        h264Profile.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
        profile.pNext = &h264Profile;
    }
    else
    {
        h265Profile.sType = VK_STRUCTURE_TYPE_VIDEO_DECODE_H265_PROFILE_INFO_KHR;
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
    fmtInfo.imageUsage = VK_IMAGE_USAGE_VIDEO_DECODE_DST_BIT_KHR | VK_IMAGE_USAGE_VIDEO_DECODE_DPB_BIT_KHR;

    uint32_t formatCount = 0;
    VkResult res = gMiniVideoProcs.getVideoFormats(engine.physicalDevice, &fmtInfo, &formatCount, nullptr);
    if (res != VK_SUCCESS || formatCount == 0)
    {
        std::cerr << "[MiniDecoder] No video formats for codec.\n";
        return std::nullopt;
    }
    std::vector<VkVideoFormatPropertiesKHR> formats(formatCount);
    for (auto& f : formats) f.sType = VK_STRUCTURE_TYPE_VIDEO_FORMAT_PROPERTIES_KHR;
    res = gMiniVideoProcs.getVideoFormats(engine.physicalDevice, &fmtInfo, &formatCount, formats.data());
    if (res != VK_SUCCESS || formatCount == 0)
    {
        std::cerr << "[MiniDecoder] Failed to query video format properties.\n";
        return std::nullopt;
    }

    MiniDecodeProfile out{};
    out.codec = codec;
    out.profile = profile;
    if (codec == MiniCodec::H264)
    {
        out.h264 = h264Profile;
        out.profile.pNext = &out.h264;
    }
    else
    {
        out.h265 = h265Profile;
        out.profile.pNext = &out.h265;
    }
    out.decodeFormat = formats[0].format;
    std::cout << "[MiniDecoder] Selected codec "
              << (codec == MiniCodec::H264 ? "H.264" : "H.265")
              << " with " << formatCount << " supported decode formats. Using format " << out.decodeFormat << "\n";
    return out;
}
