#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <optional>
#include <string>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/vec2.hpp>

#include <vulkan/vulkan.h>

#include "display2d.h"
#include "engine.h"
#include "light.h"
#include "overlay.hpp"
#include "grading.hpp"
#include "utils.h"
#include "video.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace
{
// Look for the sample video in the current directory (files were moved up).
const std::filesystem::path kDefaultVideoPath = std::filesystem::path("P1090533_main8_hevc_fast.mkv");
constexpr uint32_t kScrubberWidth = 512;
constexpr uint32_t kScrubberHeight = 64;

using overlay::FpsOverlayResources;
using overlay::ImageResource;
using overlay::OverlayCompute;
using overlay::OverlayResources;
using overlay::destroyImageResource;
using overlay::destroyOverlayCompute;
using overlay::initializeOverlayCompute;
using overlay::runOverlayCompute;
using overlay::updateFpsOverlay;
using overlay::uploadImageData;

struct VideoResources
{
    VideoImageSet descriptors;
    ImageResource lumaImage;
    ImageResource chromaImage;
    VkSampler sampler = VK_NULL_HANDLE;
};

struct VideoPlaybackState
{
    Engine* engine = nullptr;
    video::VideoDecoder decoder;
    VideoResources video;
    OverlayResources overlay;
    FpsOverlayResources fpsOverlay;
    video::VideoColorInfo colorInfo;
    std::deque<video::DecodedFrame> pendingFrames;
    video::DecodedFrame stagingFrame;
    bool playbackClockInitialized = false;
    double basePtsSeconds = 0.0;
    double lastFramePtsSeconds = 0.0;
    double lastDisplayedSeconds = 0.0;
    std::chrono::steady_clock::time_point lastFrameRenderTime{};
};

enum class DebugCategory
{
    Decode,
    Cleanup,
    Overlay
};

struct DebugFlags
{
    bool decode = false;
    bool cleanup = false;
    bool overlay = false;
} gDebugFlags;

bool isDebugEnabled(DebugCategory category)
{
    switch (category)
    {
    case DebugCategory::Decode:
        return gDebugFlags.decode;
    case DebugCategory::Cleanup:
        return gDebugFlags.cleanup;
    case DebugCategory::Overlay:
        return gDebugFlags.overlay;
    default:
        return false;
    }
}

void parseDebugFlags(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
    {
        std::string arg(argv[i] ? argv[i] : "");
        if (arg == "--debugDecode")
        {
            gDebugFlags.decode = true;
        }
        else if (arg == "--debugCleanup")
        {
            gDebugFlags.cleanup = true;
        }
        else if (arg == "--debugOverlay")
        {
            gDebugFlags.overlay = true;
        }
        else if (arg == "--debugAll")
        {
            gDebugFlags.decode = gDebugFlags.cleanup = gDebugFlags.overlay = true;
        }
    }
}

bool isDebugArg(const std::string& arg)
{
    return arg == "--debugDecode" || arg == "--debugCleanup" || arg == "--debugOverlay" || arg == "--debugAll";
}

struct CliOptions
{
    std::filesystem::path videoPath;
    std::optional<bool> swapUV;
    bool decodeOnly = false;
};

CliOptions parseCliOptions(int argc, char** argv)
{
    std::filesystem::path videoPath = kDefaultVideoPath;
    std::optional<bool> swapUV;
    bool decodeOnly = false;
    for (int i = 1; i < argc; ++i)
    {
        std::string arg(argv[i] ? argv[i] : "");
        if (arg.empty())
        {
            continue;
        }
        if (isDebugArg(arg))
        {
            continue;
        }

        if (arg == "--video" && i + 1 < argc)
        {
            std::string nextArg(argv[i + 1] ? argv[i + 1] : "");
            if (!nextArg.empty() && nextArg[0] != '-')
            {
                videoPath = std::filesystem::path(nextArg);
                ++i;
            }
            continue;
        }
        if (arg.rfind("--video=", 0) == 0)
        {
            videoPath = std::filesystem::path(arg.substr(std::string("--video=").size()));
            continue;
        }
        if (arg[0] != '-')
        {
            videoPath = std::filesystem::path(arg);
        }
        else if (arg == "--swapUV")
        {
            swapUV = true;
        }
        else if (arg == "--noSwapUV")
        {
            swapUV = false;
        }
        else if (arg == "--decodeOnly")
        {
            decodeOnly = true;
        }
    }
    if (videoPath.empty())
    {
        videoPath = kDefaultVideoPath;
    }
    return CliOptions{videoPath, swapUV, decodeOnly};
}

bool uploadDecodedFrame(VideoResources& video,
                        Engine* engine,
                        const video::VideoDecoder& decoder,
                        const video::DecodedFrame& frame);

int runDecodeOnlyBenchmark(const std::filesystem::path& videoPath, const std::optional<bool>& swapUvOverride)
{
    constexpr double kBenchmarkSeconds = 5.0; // default window to measure decode speed
    if (!std::filesystem::exists(videoPath))
    {
        std::cerr << "[DecodeOnly] Missing video file: " << videoPath << std::endl;
        return 1;
    }

    video::VideoDecoder decoder;
    video::DecoderInitParams params{};
    params.implementation = video::DecodeImplementation::Vulkan;

    if (!video::initializeVideoDecoder(videoPath, decoder, params))
    {
        std::cerr << "[DecodeOnly] Vulkan decode unavailable, falling back to software." << std::endl;
        params.implementation = video::DecodeImplementation::Software;
        if (!video::initializeVideoDecoder(videoPath, decoder, params))
        {
            std::cerr << "[DecodeOnly] Failed to initialize decoder." << std::endl;
            return 1;
        }
    }

    if (swapUvOverride.has_value())
    {
        decoder.swapChromaUV = swapUvOverride.value();
    }

    video::DecodedFrame frame;
    frame.buffer.reserve(static_cast<size_t>(decoder.bufferSize));

    auto start = std::chrono::steady_clock::now();
    size_t framesDecoded = 0;
    double firstPts = -1.0;
    while (video::decodeNextFrame(decoder, frame, /*copyFrameBuffer=*/false))
    {
        framesDecoded++;
        if (firstPts < 0.0)
        {
            firstPts = frame.ptsSeconds;
        }

        // Stop after decoding the first kBenchmarkSeconds worth of video.
        double elapsedPts = frame.ptsSeconds - (firstPts < 0.0 ? 0.0 : firstPts);
        if (elapsedPts >= kBenchmarkSeconds)
        {
            break;
        }

        frame.buffer.clear();
    }
    auto end = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(end - start).count();
    double fps = seconds > 0.0 ? static_cast<double>(framesDecoded) / seconds : 0.0;

    std::cout << "[DecodeOnly] Decoded " << framesDecoded << " frames in " << seconds
              << "s -> " << fps << " fps using " << decoder.implementationName
              << " over ~" << kBenchmarkSeconds << "s of content" << std::endl;

    video::cleanupVideoDecoder(decoder);
    return 0;
}

VkSampler createLinearClampSampler(Engine* engine)
{
    VkSampler sampler = VK_NULL_HANDLE;
    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = engine->props.limits.maxSamplerAnisotropy;
    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    if (vkCreateSampler(engine->logicalDevice, &samplerInfo, nullptr, &sampler) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create sampler.");
    }
    return sampler;
}

// Scroll handling for rectangle sizing
static double g_scrollDelta = 0.0;
static void onScroll(GLFWwindow* /*window*/, double /*xoffset*/, double yoffset)
{
    g_scrollDelta += yoffset;
}

static void onKey(GLFWwindow* window, int key, int /*scancode*/, int action, int /*mods*/)
{
    /*if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        std::cout << "[Video2D] ESC pressed at "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::steady_clock::now().time_since_epoch())
                         .count()
                  << " ms" << std::endl;
        glfwSetWindowShouldClose(window, GLFW_TRUE);
    }*/
}

bool initializeVideoPlayback(const std::filesystem::path& videoPath,
                             Engine* engine,
                             VideoPlaybackState& state,
                             double& durationSeconds,
                             const std::optional<bool>& swapUvOverride = std::nullopt)
{
    state.engine = engine;
    if (!std::filesystem::exists(videoPath))
    {
        std::cerr << "[Video2D] Missing video file: " << videoPath << std::endl;
        return false;
    }

    video::DecoderInitParams params{};
    params.implementation = video::DecodeImplementation::Vulkan;
    if (!video::initializeVideoDecoder(videoPath, state.decoder, params))
    {
        std::cerr << "[Video2D] Vulkan decode unavailable, falling back to software." << std::endl;
        params.implementation = video::DecodeImplementation::Software;
        if (!video::initializeVideoDecoder(videoPath, state.decoder, params))
        {
            std::cerr << "[Video2D] Failed to initialize decoder" << std::endl;
            return false;
        }
    }
    if (state.decoder.implementation != video::DecodeImplementation::Vulkan)
    {
        std::cerr << "[Video2D] Warning: hardware Vulkan decode not active; using "
                  << state.decoder.implementationName << std::endl;
    }

    durationSeconds = 0.0;
    if (state.decoder.formatCtx && state.decoder.formatCtx->duration > 0)
    {
        durationSeconds = static_cast<double>(state.decoder.formatCtx->duration) / static_cast<double>(AV_TIME_BASE);
    }

    state.colorInfo = video::deriveVideoColorInfo(state.decoder);

    try
    {
        state.video.sampler = createLinearClampSampler(engine);
        state.overlay.sampler = createLinearClampSampler(engine);
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[Video2D] Failed to create samplers: " << ex.what() << std::endl;
        video::cleanupVideoDecoder(state.decoder);
        return false;
    }

    video::DecodedFrame initialDecoded{};
    initialDecoded.buffer.assign(static_cast<size_t>(state.decoder.bufferSize), 0);
    if (state.decoder.outputFormat == PrimitiveYuvFormat::NV12)
    {
        const size_t yBytes = state.decoder.yPlaneBytes;
        if (yBytes > 0 && yBytes <= initialDecoded.buffer.size())
        {
            std::fill(initialDecoded.buffer.begin(), initialDecoded.buffer.begin() + yBytes, 0x80);
            std::fill(initialDecoded.buffer.begin() + yBytes, initialDecoded.buffer.end(), 0x80);
        }
    }
    else
    {
        const size_t yBytes = state.decoder.yPlaneBytes;
        const size_t uvBytes = state.decoder.uvPlaneBytes;
        const bool sixteenBit = state.decoder.bytesPerComponent > 1;
        if (sixteenBit)
        {
            const uint32_t bitDepth = state.decoder.bitDepth > 0 ? state.decoder.bitDepth : 8;
            const uint32_t shift = bitDepth >= 16 ? 0u : 16u - bitDepth;
            const uint16_t baseValue = static_cast<uint16_t>(1u << (bitDepth > 0 ? bitDepth - 1 : 7));
            const uint16_t fillValue = static_cast<uint16_t>(baseValue << shift);
            if (yBytes >= sizeof(uint16_t))
            {
                uint16_t* yDst = reinterpret_cast<uint16_t*>(initialDecoded.buffer.data());
                std::fill(yDst, yDst + (yBytes / sizeof(uint16_t)), fillValue);
            }
            if (uvBytes >= sizeof(uint16_t))
            {
                uint16_t* uvDst = reinterpret_cast<uint16_t*>(initialDecoded.buffer.data() + yBytes);
                std::fill(uvDst, uvDst + (uvBytes / sizeof(uint16_t)), fillValue);
            }
        }
        else
        {
            if (yBytes > 0 && yBytes <= initialDecoded.buffer.size())
            {
                std::fill(initialDecoded.buffer.begin(), initialDecoded.buffer.begin() + yBytes, 0x80);
            }
            if (uvBytes > 0 && yBytes + uvBytes <= initialDecoded.buffer.size())
            {
                std::fill(initialDecoded.buffer.begin() + yBytes, initialDecoded.buffer.begin() + yBytes + uvBytes, 0x80);
            }
        }
    }

    if (!uploadDecodedFrame(state.video, engine, state.decoder, initialDecoded))
    {
        std::cerr << "[Video2D] Failed to upload initial frame." << std::endl;
        video::cleanupVideoDecoder(state.decoder);
        destroyImageResource(engine, state.video.lumaImage);
        destroyImageResource(engine, state.video.chromaImage);
        if (state.video.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(engine->logicalDevice, state.video.sampler, nullptr);
            state.video.sampler = VK_NULL_HANDLE;
        }
        if (state.overlay.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(engine->logicalDevice, state.overlay.sampler, nullptr);
            state.overlay.sampler = VK_NULL_HANDLE;
        }
        return false;
    }

    if (!video::startAsyncDecoding(state.decoder, 12))
    {
        std::cerr << "[Video2D] Failed to start async decoder" << std::endl;
        video::cleanupVideoDecoder(state.decoder);
        destroyImageResource(engine, state.video.lumaImage);
        destroyImageResource(engine, state.video.chromaImage);
        if (state.video.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(engine->logicalDevice, state.video.sampler, nullptr);
            state.video.sampler = VK_NULL_HANDLE;
        }
        if (state.overlay.sampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(engine->logicalDevice, state.overlay.sampler, nullptr);
            state.overlay.sampler = VK_NULL_HANDLE;
        }
        return false;
    }
    if (isDebugEnabled(DebugCategory::Decode))
    {
        std::cout << "[Video2D] Loaded video " << videoPath
                  << " (" << state.decoder.width << "x" << state.decoder.height
                  << "), fps=" << state.decoder.fps << std::endl;
    }

    if (swapUvOverride.has_value())
    {
        state.decoder.swapChromaUV = swapUvOverride.value();
        std::cout << "[Video2D] Forcing UV swap to "
                  << (state.decoder.swapChromaUV ? "ON" : "OFF") << std::endl;
    }

    state.stagingFrame.buffer.reserve(static_cast<size_t>(state.decoder.bufferSize));
    state.pendingFrames.clear();
    state.playbackClockInitialized = false;
    state.lastDisplayedSeconds = 0.0;
    return true;
}

void pumpDecodedFrames(VideoPlaybackState& state)
{
    constexpr size_t kMaxPendingFrames = 6;
    while (state.pendingFrames.size() < kMaxPendingFrames &&
           video::acquireDecodedFrame(state.decoder, state.stagingFrame))
    {
        state.pendingFrames.emplace_back(std::move(state.stagingFrame));
        state.stagingFrame = video::DecodedFrame{};
        state.stagingFrame.buffer.reserve(static_cast<size_t>(state.decoder.bufferSize));
    }
}

bool uploadDecodedFrame(VideoResources& video,
                        Engine* engine,
                        const video::VideoDecoder& decoder,
                        const video::DecodedFrame& frame)
{
    if (!engine || frame.buffer.empty())
    {
        return false;
    }

    if (decoder.outputFormat == PrimitiveYuvFormat::NV12)
    {
        const size_t ySize = decoder.yPlaneBytes;
        const size_t uvSize = decoder.uvPlaneBytes;
        if (frame.buffer.size() < ySize + uvSize)
        {
            std::cerr << "[Video2D] NV12 frame smaller than expected." << std::endl;
            return false;
        }
        const uint8_t* yPlane = frame.buffer.data();
        const uint8_t* uvPlane = yPlane + ySize;
        if (!uploadImageData(engine,
                             video.lumaImage,
                             yPlane,
                             ySize,
                             decoder.width,
                             decoder.height,
                             VK_FORMAT_R8_UNORM))
        {
            return false;
        }
        if (!uploadImageData(engine,
                             video.chromaImage,
                             uvPlane,
                             uvSize,
                             decoder.chromaWidth,
                             decoder.chromaHeight,
                             VK_FORMAT_R8G8_UNORM))
        {
            return false;
        }
    }
    else
    {
        const uint8_t* yPlane = frame.buffer.data();
        const uint8_t* uvPlane = yPlane + decoder.yPlaneBytes;
        const bool sixteenBit = decoder.bytesPerComponent > 1;
        const VkFormat lumaFormat = sixteenBit ? VK_FORMAT_R16_UNORM : VK_FORMAT_R8_UNORM;
        const VkFormat chromaFormat = sixteenBit ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R8G8_UNORM;
        if (!uploadImageData(engine,
                             video.lumaImage,
                             yPlane,
                             decoder.yPlaneBytes,
                             decoder.width,
                             decoder.height,
                             lumaFormat))
        {
            return false;
        }
        if (!uploadImageData(engine,
                             video.chromaImage,
                             uvPlane,
                             decoder.uvPlaneBytes,
                             decoder.chromaWidth,
                             decoder.chromaHeight,
                             chromaFormat))
        {
            return false;
        }
    }

    video.descriptors.width = decoder.width;
    video.descriptors.height = decoder.height;
    video.descriptors.chromaDivX = decoder.chromaDivX;
    video.descriptors.chromaDivY = decoder.chromaDivY;
    video.descriptors.luma.view = video.lumaImage.view;
    video.descriptors.luma.sampler = video.sampler;
    video.descriptors.chroma.view = video.chromaImage.view ? video.chromaImage.view : video.lumaImage.view;
    video.descriptors.chroma.sampler = video.sampler;
    return true;
}

double advancePlayback(VideoPlaybackState& state, bool playing)
{
    if (state.video.sampler == VK_NULL_HANDLE)
    {
        return 0.0;
    }

    pumpDecodedFrames(state);

    if (!playing)
    {
        return state.lastDisplayedSeconds;
    }

    if (state.pendingFrames.empty())
    {
        if (state.decoder.finished.load() && !state.decoder.threadRunning.load() &&
            isDebugEnabled(DebugCategory::Decode))
        {
            std::cout << "[Video2D] End of video reached" << std::endl;
        }
        return state.lastDisplayedSeconds;
    }

    auto currentTime = std::chrono::steady_clock::now();
    auto& nextFrame = state.pendingFrames.front();

    if (!state.playbackClockInitialized)
    {
        state.playbackClockInitialized = true;
        state.basePtsSeconds = nextFrame.ptsSeconds;
        state.lastFramePtsSeconds = nextFrame.ptsSeconds;
        state.lastFrameRenderTime = currentTime;
    }

    double frameDelta = nextFrame.ptsSeconds - state.lastFramePtsSeconds;
    if (frameDelta < 1e-6)
    {
        frameDelta = 1.0 / std::max(30.0, state.decoder.fps);
    }

    auto targetTime = state.lastFrameRenderTime + std::chrono::duration<double>(frameDelta);
    if (currentTime + std::chrono::milliseconds(1) < targetTime)
    {
        return state.lastDisplayedSeconds;
    }

    auto frame = std::move(nextFrame);
    state.pendingFrames.pop_front();

    if (!uploadDecodedFrame(state.video, state.engine, state.decoder, frame))
    {
        std::cerr << "[Video2D] Failed to upload decoded frame." << std::endl;
    }

    state.lastFramePtsSeconds = frame.ptsSeconds;
    state.lastFrameRenderTime = currentTime;
    state.lastDisplayedSeconds = std::max(0.0, state.lastFramePtsSeconds - state.basePtsSeconds);
    return state.lastDisplayedSeconds;
}

// Scrubber GPU drawing is now handled inside the video_blit compute shader; the
// standalone scrubber compute path has been removed.

bool cursorInScrubber(double x, double y, int windowWidth, int windowHeight)
{
    // Use the actual scrubber pixel size to align hit testing with the overlay quad
    const double scrubberWidth = static_cast<double>(kScrubberWidth);
    const double scrubberHeight = static_cast<double>(kScrubberHeight);
    const double margin = 20.0; // small bottom padding
    const double left = (static_cast<double>(windowWidth) - scrubberWidth) * 0.5;
    const double right = left + scrubberWidth;
    const double top = static_cast<double>(windowHeight) - scrubberHeight - margin;
    const double bottom = top + scrubberHeight;
    return x >= left && x <= right && y >= top && y <= bottom;
}
}

int main(int argc, char** argv)
{
    parseDebugFlags(argc, argv);
    CliOptions cli = parseCliOptions(argc, argv);

    if (cli.decodeOnly)
    {
        std::cout << "[DecodeOnly] Running decode benchmark for " << cli.videoPath << std::endl;
        return runDecodeOnlyBenchmark(cli.videoPath, cli.swapUV);
    }

    Engine* engine = nullptr;
    Display2D* display = nullptr;
    Display2D* cropDisplay = nullptr;
    Display2D* gradingDisplay = nullptr;
    try {
        engine = new Engine();
        std::cout << "[Video2D] Initializing display..." << std::endl;
        display = new Display2D(engine, 1280, 720, "Motive Video 2D");
        std::cout << "[Video2D] Display initialized successfully." << std::endl;
        cropDisplay = new Display2D(engine, 360, 640, "Region View");
        gradingDisplay = new Display2D(engine, 420, 520, "Grading");
    } catch (const std::exception& ex) {
        std::cerr << "[Video2D] FATAL: Exception during engine or display initialization: " << ex.what() << std::endl;
        if (display)
        {
            delete display;
        }
        if (cropDisplay)
        {
            delete cropDisplay;
        }
        if (gradingDisplay)
        {
            delete gradingDisplay;
        }
        delete engine;
        return 1;
    }
    int fbWidth = 0;
    int fbHeight = 0;
    glfwGetFramebufferSize(display->window, &fbWidth, &fbHeight);
    fbWidth = std::max(1, fbWidth);
    fbHeight = std::max(1, fbHeight);
    float fbWidthF = static_cast<float>(fbWidth);
    float fbHeightF = static_cast<float>(fbHeight);
    glfwSetScrollCallback(display->window, onScroll);
    //glfwSetKeyCallback(display->window, onKey);

    Light sceneLight(glm::vec3(0.0f, 0.0f, 1.0f),
                     glm::vec3(0.1f),
                     glm::vec3(0.9f));
    sceneLight.setDiffuse(glm::vec3(1.0f, 0.95f, 0.9f));
    engine->setLight(sceneLight);

    VideoPlaybackState playbackState;
    double videoDurationSeconds = 0.0;
    std::cout << "[Video2D] Using video file: " << cli.videoPath << std::endl;
    if (!initializeVideoPlayback(cli.videoPath, engine, playbackState, videoDurationSeconds, cli.swapUV))
    {
        delete engine;
        return 1;
    }

    OverlayCompute overlayCompute{};
    if (!initializeOverlayCompute(engine, overlayCompute))
    {
        video::cleanupVideoDecoder(playbackState.decoder);
        delete engine;
        return 1;
    }

    VkSampler blackSampler = VK_NULL_HANDLE;
    // Black dummy video for overlay-only window
    overlay::ImageResource blackLuma;
    overlay::ImageResource blackChroma;
    VideoImageSet blackVideo{};

    // Prepare black dummy video resources (1x1) for grading-only window
    {
        uint8_t luma = 0;
        uint8_t chroma[2] = {128, 128};
        overlay::uploadImageData(engine, blackLuma, &luma, sizeof(luma), 1, 1, VK_FORMAT_R8_UNORM);
        overlay::uploadImageData(engine, blackChroma, chroma, sizeof(chroma), 1, 1, VK_FORMAT_R8G8_UNORM);
        try
        {
            blackSampler = createLinearClampSampler(engine);
        }
        catch (const std::exception&)
        {
            blackSampler = playbackState.overlay.sampler;
        }
        blackVideo.width = 1;
        blackVideo.height = 1;
        blackVideo.chromaDivX = 1;
        blackVideo.chromaDivY = 1;
        blackVideo.luma.view = blackLuma.view;
        blackVideo.luma.sampler = blackSampler;
        blackVideo.chroma.view = blackChroma.view;
        blackVideo.chroma.sampler = blackSampler;
    }

    bool playing = true;
    bool spaceHeld = false;
    bool mouseHeld = false;
    bool scrubDragging = false;
    double scrubDragStartX = 0.0;
    float scrubDragStartProgress = 0.0f;
    double scrubProgressUi = 0.0;
    GradingSettings gradingSettings{};
    grading::setGradingDefaults(gradingSettings);
    const std::filesystem::path gradingConfigPath("blit_settings.json");
    bool gradingLoaded = grading::loadGradingSettings(gradingConfigPath, gradingSettings);
    if (!gradingLoaded && std::filesystem::exists(gradingConfigPath))
    {
        std::cerr << "[Video2D] Failed to parse grading settings from " << gradingConfigPath << ", using defaults.\n";
    }
    overlay::ImageResource gradingOverlayImage;
    OverlayImageInfo gradingOverlayInfo{};
    grading::SliderLayout gradingLayout{};
    bool gradingOverlayDirty = true;
    uint32_t gradingFbWidth = 0;
    uint32_t gradingFbHeight = 0;
    bool gradingMouseHeld = false;
    int lastGradingSlider = -1;
    auto lastGradingClickTime = std::chrono::steady_clock::time_point{};
    bool gradingPreviewEnabled = true;
    auto fpsLastSample = std::chrono::steady_clock::now();
    int fpsFrameCounter = 0;
    float currentFps = 0.0f;
    // Rectangle state
    float rectHeight = fbHeightF;
    float rectWidth = rectHeight * (9.0f / 16.0f);
    glm::vec2 rectCenter(fbWidthF * 0.5f, fbHeightF * 0.5f);

    while (true)
    {
        display->pollEvents();
        cropDisplay->pollEvents();
        gradingDisplay->pollEvents();
        // Update framebuffer/window sizes for both windows
        glfwGetFramebufferSize(display->window, &fbWidth, &fbHeight);
        int cropFbWidth = 0;
        int cropFbHeight = 0;
        glfwGetFramebufferSize(cropDisplay->window, &cropFbWidth, &cropFbHeight);
        int gradingFbWidthInt = 0;
        int gradingFbHeightInt = 0;
        glfwGetFramebufferSize(gradingDisplay->window, &gradingFbWidthInt, &gradingFbHeightInt);
        uint32_t prevGradingFbW = gradingFbWidth;
        uint32_t prevGradingFbH = gradingFbHeight;
        glfwGetWindowSize(display->window, &display->width, &display->height);
        glfwGetWindowSize(cropDisplay->window, &cropDisplay->width, &cropDisplay->height);
        glfwGetWindowSize(gradingDisplay->window, &gradingDisplay->width, &gradingDisplay->height);
        fbWidth = std::max(1, fbWidth);
        fbHeight = std::max(1, fbHeight);
        cropFbWidth = std::max(1, cropFbWidth);
        cropFbHeight = std::max(1, cropFbHeight);
        gradingFbWidth = std::max(1, gradingFbWidthInt);
        gradingFbHeight = std::max(1, gradingFbHeightInt);
        if (gradingFbWidth != prevGradingFbW || gradingFbHeight != prevGradingFbH)
        {
            gradingOverlayDirty = true;
        }
        fbWidthF = static_cast<float>(fbWidth);
        fbHeightF = static_cast<float>(fbHeight);
        display->width = std::max(1, display->width);
        display->height = std::max(1, display->height);
        float windowWidthF = static_cast<float>(std::max(1, display->width));
        float windowHeightF = static_cast<float>(std::max(1, display->height));
        float cursorScaleX = fbWidthF / windowWidthF;
        float cursorScaleY = fbHeightF / windowHeightF;
        rectHeight = std::min(rectHeight, fbHeightF);
        rectWidth = rectHeight * (9.0f / 16.0f);
        rectCenter.x = std::clamp(rectCenter.x, 0.0f, fbWidthF);
        rectCenter.y = std::clamp(rectCenter.y, 0.0f, fbHeightF);
        const uint32_t fbWidthU = static_cast<uint32_t>(fbWidth);
        const uint32_t fbHeightU = static_cast<uint32_t>(fbHeight);
        if ((playbackState.fpsOverlay.lastRefWidth != fbWidthU ||
             playbackState.fpsOverlay.lastRefHeight != fbHeightU) &&
            playbackState.fpsOverlay.lastFpsValue >= 0.0f)
        {
            updateFpsOverlay(engine,
                             playbackState.fpsOverlay,
                             playbackState.overlay.sampler,
                             playbackState.video.sampler,
                             playbackState.fpsOverlay.lastFpsValue,
                             fbWidthU,
                             fbHeightU);
        }

        if (glfwGetKey(display->window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
        {                
            std::cout << "ESC DETECTED\n";
            glfwSetWindowShouldClose(display->window, GLFW_TRUE);
            break;
        }

        if (display->shouldClose() || cropDisplay->shouldClose() || gradingDisplay->shouldClose())
        {
            break;
        }

        bool spaceDown = glfwGetKey(display->window, GLFW_KEY_SPACE) == GLFW_PRESS;
        if (spaceDown && !spaceHeld)
        {
            playing = !playing;
        }
        spaceHeld = spaceDown;

        int mouseState = glfwGetMouseButton(display->window, GLFW_MOUSE_BUTTON_LEFT);
        if (mouseState == GLFW_PRESS && !mouseHeld)
        {
            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(display->window, &cursorX, &cursorY);
            if (cursorInScrubber(cursorX, cursorY, display->width, display->height))
            {
                scrubDragging = true;
                scrubDragStartX = cursorX;
                scrubDragStartProgress = static_cast<float>(scrubProgressUi);
                playing = false; // pause while dragging
            }
            else
            {
                rectCenter = glm::vec2(static_cast<float>(cursorX * cursorScaleX),
                                       static_cast<float>(cursorY * cursorScaleY));
                if (isDebugEnabled(DebugCategory::Overlay))
                {
                    std::cout << "[Video2D] Rectangle recentered to (" << rectCenter.x << ", " << rectCenter.y
                              << ") size=(" << rectWidth << " x " << rectHeight << ")\n";
                }
            }
        }
        else if (mouseState == GLFW_PRESS && scrubDragging)
        {
            double cursorX = 0.0;
            double cursorY = 0.0;
            glfwGetCursorPos(display->window, &cursorX, &cursorY);
            double deltaX = cursorX - scrubDragStartX;
            double scrubberWidth = static_cast<double>(kScrubberWidth);
            float deltaProgress = static_cast<float>(deltaX / scrubberWidth);
            float newProgress = std::clamp(scrubDragStartProgress + deltaProgress, 0.0f, 1.0f);
            scrubProgressUi = newProgress;
        }
        else if (mouseState == GLFW_RELEASE && scrubDragging)
        {
            scrubDragging = false;
            if (videoDurationSeconds > 0.0)
            {
                playbackState.lastDisplayedSeconds = scrubProgressUi * videoDurationSeconds;
                double targetSeekSeconds = playbackState.lastDisplayedSeconds;

                // Stop async decoding, seek, then restart
                video::stopAsyncDecoding(playbackState.decoder);
                if (!video::seekVideoDecoder(playbackState.decoder, targetSeekSeconds))
                {
                    std::cerr << "[Video2D] Failed to seek video decoder." << std::endl;
                }

                // After seeking, the next frames decoded might have different dimensions,
                // which will trigger re-creation of the backing image resources. To avoid
                // destroying an image that is still in-flight on the GPU, we must wait
                // for the device to be idle before proceeding. A more sophisticated solution
                // would use multiple image buffers or a deferred destruction queue.
                vkDeviceWaitIdle(engine->logicalDevice);

                // Clear pending frames as they are now invalid after seeking
                playbackState.pendingFrames.clear();
                playbackState.playbackClockInitialized = false; // Re-initialize clock after seek
                // The lastDisplayedSeconds, basePtsSeconds, lastFramePtsSeconds will be set by the first frame after seek
                video::startAsyncDecoding(playbackState.decoder, 12); // Restart async decoding
            }
        }
        mouseHeld = (mouseState == GLFW_PRESS);

        // Grading window slider interaction (supports double-click to reset that slider)
        int gradingMouseState = glfwGetMouseButton(gradingDisplay->window, GLFW_MOUSE_BUTTON_LEFT);
        if (gradingMouseState == GLFW_PRESS && !gradingMouseHeld)
        {
            double gx = 0.0;
            double gy = 0.0;
            glfwGetCursorPos(gradingDisplay->window, &gx, &gy);

            auto classifyHit = [&](double x, double y) -> int {
                const double relX = x - static_cast<double>(gradingLayout.offset.x);
                const double relY = y - static_cast<double>(gradingLayout.offset.y);
                if (relX < 0.0 || relY < 0.0 || relX >= gradingLayout.width || relY >= gradingLayout.height)
                {
                    return -1;
                }
                if (relX >= gradingLayout.resetX0 && relX <= gradingLayout.resetX1 &&
                    relY >= gradingLayout.resetY0 && relY <= gradingLayout.resetY1)
                {
                    return -2; // reset button
                }
                if (relX >= gradingLayout.saveX0 && relX <= gradingLayout.saveX1 &&
                    relY >= gradingLayout.saveY0 && relY <= gradingLayout.saveY1)
                {
                    return -3; // save button
                }
                if (relX >= gradingLayout.previewX0 && relX <= gradingLayout.previewX1 &&
                    relY >= gradingLayout.previewY0 && relY <= gradingLayout.previewY1)
                {
                    return -4; // preview toggle
                }
                const uint32_t padding = 12;
                const uint32_t barWidth = gradingLayout.width - padding * 2;
                const uint32_t barYStart = gradingLayout.barYStart;
                const uint32_t rowHeight = gradingLayout.barHeight + gradingLayout.rowSpacing;
                int sliderIndex = static_cast<int>((relY - barYStart) / rowHeight);
                if (sliderIndex < 0 || sliderIndex >= 12)
                {
                    return -1;
                }
                const double localX = relX - padding;
                if (localX < 0.0 || localX > static_cast<double>(barWidth))
                {
                    return -1;
                }
                return sliderIndex;
            };

            int hitIndex = classifyHit(gx, gy);
            auto now = std::chrono::steady_clock::now();
            bool doubleClick = false;
            constexpr auto kDoubleClickWindow = std::chrono::milliseconds(300);
            if (hitIndex >= 0 &&
                hitIndex == lastGradingSlider &&
                lastGradingClickTime.time_since_epoch().count() > 0 &&
                (now - lastGradingClickTime) <= kDoubleClickWindow)
            {
                doubleClick = true;
            }

            bool saveRequested = false;
            bool previewToggle = false;
            if (grading::handleOverlayClick(gradingLayout,
                                            gx,
                                            gy,
                                            gradingSettings,
                                            doubleClick,
                                            &saveRequested,
                                            &previewToggle))
            {
                gradingOverlayDirty = true;
                if (saveRequested)
                {
                    if (!grading::saveGradingSettings(gradingConfigPath, gradingSettings))
                    {
                        std::cerr << "[Video2D] Failed to save grading settings to " << gradingConfigPath << "\n";
                    }
                }
                if (previewToggle)
                {
                    gradingPreviewEnabled = !gradingPreviewEnabled;
                }
            }

            lastGradingSlider = hitIndex;
            lastGradingClickTime = now;
            gradingMouseHeld = true;
        }
        else if (gradingMouseState == GLFW_PRESS && gradingMouseHeld)
        {
            double gx = 0.0;
            double gy = 0.0;
            glfwGetCursorPos(gradingDisplay->window, &gx, &gy);
            bool saveRequested = false;
            bool previewToggle = false;
            if (grading::handleOverlayClick(gradingLayout, gx, gy, gradingSettings, false, &saveRequested, &previewToggle))
            {
                gradingOverlayDirty = true;
                if (saveRequested)
                {
                    if (!grading::saveGradingSettings(gradingConfigPath, gradingSettings))
                    {
                        std::cerr << "[Video2D] Failed to save grading settings to " << gradingConfigPath << "\n";
                    }
                }
                if (previewToggle)
                {
                    gradingPreviewEnabled = !gradingPreviewEnabled;
                }
            }
        }
        else if (gradingMouseState == GLFW_RELEASE)
        {
            gradingMouseHeld = false;
        }

        // Scroll to resize rectangle
        double scrollDelta = g_scrollDelta;
        g_scrollDelta = 0.0;
        if (std::abs(scrollDelta) > 0.0)
        {
            float scale = 1.0f + static_cast<float>(scrollDelta) * 0.05f;
            rectHeight = std::clamp(rectHeight * scale, 50.0f, fbHeightF);
            rectWidth = rectHeight * (9.0f / 16.0f);
        }

        double playbackSeconds = advancePlayback(playbackState, playing && !scrubDragging);
        double normalizedProgress = videoDurationSeconds > 0.0
                                        ? std::clamp(playbackSeconds / videoDurationSeconds, 0.0, 1.0)
                                        : 0.0;
        if (!scrubDragging)
        {
            scrubProgressUi = normalizedProgress;
        }
        const float displayProgress = static_cast<float>(scrubProgressUi);

        // Rebuild grading overlay if needed or size changed
        if (gradingOverlayDirty)
        {
            gradingOverlayDirty = grading::buildGradingOverlay(engine,
                                                               gradingSettings,
                                                               gradingOverlayImage,
                                                               gradingOverlayInfo,
                                                               gradingFbWidth,
                                                               gradingFbHeight,
                                                               gradingLayout,
                                                               gradingPreviewEnabled);
            gradingOverlayInfo.overlay.sampler = playbackState.overlay.sampler;
        }
        const ColorAdjustments* activeAdjustments = gradingPreviewEnabled
                                                        ? reinterpret_cast<ColorAdjustments*>(&gradingSettings)
                                                        : nullptr;
        display->renderFrame(playbackState.video.descriptors,
                             playbackState.overlay.info,
                             playbackState.fpsOverlay.info,
                             playbackState.colorInfo,
                             displayProgress,
                             playing ? 1.0f : 0.0f,
                             nullptr,
                             activeAdjustments);
        // FPS tracking
        fpsFrameCounter++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - fpsLastSample).count();
        if (elapsed >= 500)
        {
            currentFps = static_cast<float>(fpsFrameCounter) * 1000.0f / static_cast<float>(elapsed);
            fpsFrameCounter = 0;
            fpsLastSample = now;
            updateFpsOverlay(engine,
                             playbackState.fpsOverlay,
                             playbackState.overlay.sampler,
                             playbackState.video.sampler,
                             currentFps,
                             fbWidthU,
                             fbHeightU);
        }

        // Draw overlay (rectangle) via GPU compute into overlay image
        runOverlayCompute(engine,
                          overlayCompute,
                          playbackState.overlay.image,
                          static_cast<uint32_t>(fbWidth),
                          static_cast<uint32_t>(fbHeight),
                          glm::vec2(rectCenter.x, rectCenter.y),
                          glm::vec2(rectWidth, rectHeight),
                          3.0f,
                          3.0f);
        playbackState.overlay.info.overlay.view = playbackState.overlay.image.view;
        playbackState.overlay.info.overlay.sampler = playbackState.overlay.sampler;
        playbackState.overlay.info.extent = {static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight)};
        playbackState.overlay.info.offset = {0, 0};
        playbackState.overlay.info.enabled = true;

        // Render cropped region in secondary window
                RenderOverrides overrides{};
                const uint32_t vidW = playbackState.video.descriptors.width;
                const uint32_t vidH = playbackState.video.descriptors.height;
        if (vidW > 0 && vidH > 0)
        {
            const float outputAspect = fbWidthF / fbHeightF;
            const float videoAspect = static_cast<float>(vidW) / static_cast<float>(vidH);
            float targetW = fbWidthF;
            float targetH = fbHeightF;
            if (videoAspect > outputAspect)
            {
                targetH = targetW / videoAspect;
            }
            else
            {
                targetW = targetH * videoAspect;
            }
            const float targetX = (fbWidthF - targetW) * 0.5f;
            const float targetY = (fbHeightF - targetH) * 0.5f;

            const float rectLeft = rectCenter.x - rectWidth * 0.5f;
            const float rectRight = rectCenter.x + rectWidth * 0.5f;
            const float rectTop = rectCenter.y - rectHeight * 0.5f;
            const float rectBottom = rectCenter.y + rectHeight * 0.5f;

            const float cropLeft = std::clamp(rectLeft, targetX, targetX + targetW);
            const float cropRight = std::clamp(rectRight, targetX, targetX + targetW);
            const float cropTop = std::clamp(rectTop, targetY, targetY + targetH);
            const float cropBottom = std::clamp(rectBottom, targetY, targetY + targetH);

            const float cropW = std::max(0.0f, cropRight - cropLeft);
            const float cropH = std::max(0.0f, cropBottom - cropTop);
            if (cropW > 1.0f && cropH > 1.0f)
            {
                const float u0 = (cropLeft - targetX) / targetW;
                const float v0 = (cropTop - targetY) / targetH;
                const float u1 = (cropRight - targetX) / targetW;
                const float v1 = (cropBottom - targetY) / targetH;

                overrides.useTargetOverride = true;
                overrides.targetOrigin = glm::vec2(0.0f, 0.0f);
                overrides.targetSize = glm::vec2(static_cast<float>(cropFbWidth),
                                                 static_cast<float>(cropFbHeight));
                overrides.useCrop = true;
                overrides.cropOrigin = glm::vec2(u0, v0);
                overrides.cropSize = glm::vec2(u1 - u0, v1 - v0);
                overrides.hideScrubber = true;

                OverlayImageInfo disabledOverlay{};
                OverlayImageInfo disabledFps{};
                cropDisplay->renderFrame(playbackState.video.descriptors,
                                         disabledOverlay,
                                         disabledFps,
                                         playbackState.colorInfo,
                                         0.0f,
                                         0.0f,
                                         &overrides,
                                         activeAdjustments);
            }
        }

        // Grading preview window with sliders overlay (no video)
        OverlayImageInfo gradingOverlay = gradingOverlayInfo;
        gradingOverlay.overlay.sampler = playbackState.overlay.sampler;
        OverlayImageInfo disabledFps{};
        RenderOverrides gradingOverrides{};
        gradingOverrides.hideScrubber = true;
        gradingDisplay->renderFrame(blackVideo,
                                    gradingOverlay,
                                    disabledFps,
                                    playbackState.colorInfo,
                                    0.0f,
                                    0.0f,
                                    &gradingOverrides,
                                    activeAdjustments);
    }

    const bool logCleanup = isDebugEnabled(DebugCategory::Cleanup);
    auto cleanupStart = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] Starting cleanup..." << std::endl;
    }
    
    auto start1 = std::chrono::steady_clock::now();
    destroyOverlayCompute(overlayCompute);
    auto end1 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyOverlayCompute took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end1 - start1).count() 
                  << " ms" << std::endl;
    }
    
    auto start2 = std::chrono::steady_clock::now();
    video::stopAsyncDecoding(playbackState.decoder);
    auto end2 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] stopAsyncDecoding took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end2 - start2).count() 
                  << " ms" << std::endl;
    }
    
    auto start3 = std::chrono::steady_clock::now();
    destroyImageResource(engine, playbackState.video.lumaImage);
    auto end3 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyImageResource(lumaImage) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end3 - start3).count() 
                  << " ms" << std::endl;
    }
    
    auto start4 = std::chrono::steady_clock::now();
    destroyImageResource(engine, playbackState.video.chromaImage);
    auto end4 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyImageResource(chromaImage) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end4 - start4).count() 
                  << " ms" << std::endl;
    }
    
    auto start5 = std::chrono::steady_clock::now();
    destroyImageResource(engine, playbackState.overlay.image);
    auto end5 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyImageResource(overlay.image) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end5 - start5).count() 
                  << " ms" << std::endl;
    }
    
    auto start6 = std::chrono::steady_clock::now();
    destroyImageResource(engine, playbackState.fpsOverlay.image);
    auto end6 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] destroyImageResource(fpsOverlay.image) took "
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end6 - start6).count()
                  << " ms" << std::endl;
    }
    
    auto start7 = std::chrono::steady_clock::now();
    if (playbackState.video.sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(engine->logicalDevice, playbackState.video.sampler, nullptr);
        playbackState.video.sampler = VK_NULL_HANDLE;
    }
    auto end7 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] vkDestroySampler(video) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end7 - start7).count() 
                  << " ms" << std::endl;
    }
    
    auto start8 = std::chrono::steady_clock::now();
    if (playbackState.overlay.sampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(engine->logicalDevice, playbackState.overlay.sampler, nullptr);
        playbackState.overlay.sampler = VK_NULL_HANDLE;
    }
    auto end8 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] vkDestroySampler(overlay) took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end8 - start8).count() 
                  << " ms" << std::endl;
    }
    
    auto start9 = std::chrono::steady_clock::now();
    video::cleanupVideoDecoder(playbackState.decoder);
    auto end9 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] cleanupVideoDecoder took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end9 - start9).count() 
                  << " ms" << std::endl;
    }
    
    auto start10 = std::chrono::steady_clock::now();
    if (display)
    {
        display->shutdown();
        delete display;
        display = nullptr;
    }
    if (cropDisplay)
    {
        cropDisplay->shutdown();
        delete cropDisplay;
        cropDisplay = nullptr;
    }
    if (gradingDisplay)
    {
        gradingDisplay->shutdown();
        delete gradingDisplay;
        gradingDisplay = nullptr;
    }
    destroyImageResource(engine, blackLuma);
    destroyImageResource(engine, blackChroma);
    if (blackSampler != VK_NULL_HANDLE && blackSampler != playbackState.overlay.sampler)
    {
        vkDestroySampler(engine->logicalDevice, blackSampler, nullptr);
    }
    destroyImageResource(engine, blackLuma);
    destroyImageResource(engine, blackChroma);
    auto end10 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] delete display took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end10 - start10).count() 
                  << " ms" << std::endl;
    }
    
    auto start11 = std::chrono::steady_clock::now();
    delete engine;
    auto end11 = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        std::cout << "[Video2D] delete engine took " 
                  << std::chrono::duration_cast<std::chrono::milliseconds>(end11 - start11).count() 
                  << " ms" << std::endl;
    }
    
    auto cleanupEnd = std::chrono::steady_clock::now();
    if (logCleanup)
    {
        auto totalCleanup = std::chrono::duration_cast<std::chrono::milliseconds>(cleanupEnd - cleanupStart);
        std::cout << "[Video2D] Total cleanup took " << totalCleanup.count() << " ms" << std::endl;
    }
    
    return 0;
}
