#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>
#include <chrono>

#include <vulkan/vulkan.h>

#include "video.h"
#include "overlay.hpp"

// Forward declarations
class Engine;

struct VideoResources
{
    VideoImageSet descriptors;
    overlay::ImageResource lumaImage;
    overlay::ImageResource chromaImage;
    VkSampler sampler = VK_NULL_HANDLE;
    VkImageView externalLumaView = VK_NULL_HANDLE;
    VkImageView externalChromaView = VK_NULL_HANDLE;
    bool usingExternal = false;
};

struct VideoPlaybackState
{
    Engine* engine = nullptr;
    video::VideoDecoder decoder;
    VideoResources video;
    overlay::OverlayResources overlay;
    overlay::FpsOverlayResources fpsOverlay;
    video::VideoColorInfo colorInfo;
    std::deque<video::DecodedFrame> pendingFrames;
    video::DecodedFrame stagingFrame;
    bool playbackClockInitialized = false;
    double basePtsSeconds = 0.0;
    double lastFramePtsSeconds = 0.0;
    double lastDisplayedSeconds = 0.0;
    std::chrono::steady_clock::time_point lastFrameRenderTime{};
};

// Sampler creation
VkSampler createLinearClampSampler(Engine* engine);

// External video view management
void destroyExternalVideoViews(Engine* engine, VideoResources& video);

// Vulkan frame synchronization
bool waitForVulkanFrameReady(Engine* engine, const video::DecodedFrame::VulkanSurface& surface);

// Frame upload and playback
bool uploadDecodedFrame(VideoResources& video,
                        Engine* engine,
                        const video::VideoDecoder& decoder,
                        const video::DecodedFrame& frame);

bool initializeVideoPlayback(const std::filesystem::path& videoPath,
                             Engine* engine,
                             VideoPlaybackState& state,
                             double& durationSeconds,
                             const std::optional<bool>& swapUvOverride = std::nullopt);

void pumpDecodedFrames(VideoPlaybackState& state);

double advancePlayback(VideoPlaybackState& state, bool playing);

// Benchmark function
int runDecodeOnlyBenchmark(const std::filesystem::path& videoPath, const std::optional<bool>& swapUvOverride);
