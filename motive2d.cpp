#include <algorithm>
#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <iostream>
#include <cstring>
#include <array>
#include <optional>
#include <string>
#include <sstream>
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
#include "decode.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
}

namespace
{
// Look for the sample video in the current directory (files were moved up).
const std::filesystem::path kDefaultVideoPath = std::filesystem::path("P1090533_main8_hevc_fast.mkv");
constexpr uint32_t kScrubberHeight = 64;
constexpr uint32_t kScrubberMinWidth = 200;
constexpr double kScrubberMargin = 20.0;
constexpr double kPlayIconSize = 28.0;

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
    bool encode = false;
    bool showInput = true;
    bool showRegion = true;
    bool showGrading = true;
};

CliOptions parseCliOptions(int argc, char** argv)
{
    std::filesystem::path videoPath = kDefaultVideoPath;
    std::optional<bool> swapUV;
    bool decodeOnly = false;
    bool encode = false;
    bool showInput = true;
    bool showRegion = true;
    bool showGrading = true;
    bool windowsSpecified = false;
    bool parsedInput = false;
    bool parsedRegion = false;
    bool parsedGrading = false;
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
        else if (arg == "--encode")
        {
            encode = true;
        }
        else if (arg.rfind("--windows", 0) == 0)
        {
            // Format: --windows=region,input,grading,none
            std::string list;
            if (arg == "--windows" && i + 1 < argc)
            {
                std::string nextArg(argv[i + 1] ? argv[i + 1] : "");
                if (!nextArg.empty() && nextArg[0] != '-')
                {
                    list = nextArg;
                    ++i;
                }
            }
            else if (arg.rfind("--windows=", 0) == 0)
            {
                list = arg.substr(std::string("--windows=").size());
            }

            if (!list.empty())
            {
                windowsSpecified = true;
                bool tmpInput = false;
                bool tmpRegion = false;
                bool tmpGrading = false;
                std::stringstream ss(list);
                std::string token;
                while (std::getline(ss, token, ','))
                {
                    if (token == "none")
                    {
                        tmpInput = false;
                        tmpRegion = false;
                        tmpGrading = false;
                        continue;
                    }
                    if (token == "input")
                    {
                        tmpInput = true;
                        continue;
                    }
                    if (token == "region")
                    {
                        tmpRegion = true;
                        continue;
                    }
                    if (token == "grading")
                    {
                        tmpGrading = true;
                    }
                }
                parsedInput = tmpInput;
                parsedRegion = tmpRegion;
                parsedGrading = tmpGrading;
            }
        }
    }
    if (videoPath.empty())
    {
        videoPath = kDefaultVideoPath;
    }
    if (windowsSpecified)
    {
        showInput = parsedInput;
        showRegion = parsedRegion;
        showGrading = parsedGrading;
    }
    return CliOptions{videoPath, swapUV, decodeOnly, encode, showInput, showRegion, showGrading};
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


// Scrubber GPU drawing is now handled inside the video_blit compute shader; the
// standalone scrubber compute path has been removed.

struct ScrubberUi
{
    double left;
    double top;
    double right;
    double bottom;
    double iconLeft;
    double iconTop;
    double iconRight;
    double iconBottom;
};

ScrubberUi computeScrubberUi(int windowWidth, int windowHeight)
{
    ScrubberUi ui{};
    const double availableWidth = static_cast<double>(windowWidth);
    const double scrubberWidth =
        std::max(static_cast<double>(kScrubberMinWidth),
                 availableWidth - (kPlayIconSize + kScrubberMargin * 3.0));
    const double scrubberHeight = static_cast<double>(kScrubberHeight);
    ui.iconLeft = kScrubberMargin;
    ui.iconRight = ui.iconLeft + kPlayIconSize;
    ui.top = static_cast<double>(windowHeight) - scrubberHeight - kScrubberMargin;
    ui.bottom = ui.top + scrubberHeight;
    ui.iconTop = ui.top + (scrubberHeight - kPlayIconSize) * 0.5;
    ui.iconBottom = ui.iconTop + kPlayIconSize;

    ui.left = ui.iconRight + kScrubberMargin;
    ui.right = ui.left + scrubberWidth;
    return ui;
}

bool cursorInScrubber(double x, double y, int windowWidth, int windowHeight)
{
    const ScrubberUi ui = computeScrubberUi(windowWidth, windowHeight);
    return x >= ui.left && x <= ui.right && y >= ui.top && y <= ui.bottom;
}

bool cursorInPlayButton(double x, double y, int windowWidth, int windowHeight)
{
    const ScrubberUi ui = computeScrubberUi(windowWidth, windowHeight);
    return x >= ui.iconLeft && x <= ui.iconRight && y >= ui.iconTop && y <= ui.iconBottom;
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
    if (!cli.showInput)
    {
        std::cout << "[Video2D] Windows disabled (--windows=none); running headless decode benchmark.\n";
        if (cli.encode)
        {
            std::cerr << "[Video2D] --encode is not available in headless mode yet.\n";
            return 1;
        }
        return runDecodeOnlyBenchmark(cli.videoPath, cli.swapUV);
    }

    try {
        engine = new Engine();
        std::cout << "[Video2D] Initializing display..." << std::endl;
        if (cli.showInput)
        {
            display = new Display2D(engine, 1280, 900, "Motive Video 2D");
            std::cout << "[Video2D] Display initialized successfully." << std::endl;
        }
        else
        {
            std::cout << "[Video2D] Windows disabled (--windows=none); running headless.\n";
        }
        if (cli.showRegion)
        {
            cropDisplay = new Display2D(engine, 360, 640, "Region View");
        }
        if (cli.showGrading)
        {
            gradingDisplay = new Display2D(engine, 420, 880, "Grading");
        }
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
    bool wasPlayingBeforeScrub = false;
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
    bool gradingRightHeld = false;
    int lastGradingSlider = -1;
    auto lastGradingClickTime = std::chrono::steady_clock::time_point{};
    bool gradingPreviewEnabled = true;
    bool detectionEnabled = false;
    auto fpsLastSample = std::chrono::steady_clock::now();
    int fpsFrameCounter = 0;
    float currentFps = 0.0f;
    std::array<float, kCurveLutSize> curveLut{};
    bool curveDirty = true;
    // Rectangle state
    float rectHeight = fbHeightF;
    float rectWidth = rectHeight * (9.0f / 16.0f);
    glm::vec2 rectCenter(fbWidthF * 0.5f, fbHeightF * 0.5f);

    while (true)
    {
        if (display)
        {
            display->pollEvents();
        }
        if (cropDisplay)
        {
            cropDisplay->pollEvents();
        }
        if (gradingDisplay)
        {
            gradingDisplay->pollEvents();
        }
        // Update framebuffer/window sizes for both windows
        if (display)
        {
            glfwGetFramebufferSize(display->window, &fbWidth, &fbHeight);
            glfwGetWindowSize(display->window, &display->width, &display->height);
        }
        int cropFbWidth = 0;
        int cropFbHeight = 0;
        if (cropDisplay)
        {
            glfwGetFramebufferSize(cropDisplay->window, &cropFbWidth, &cropFbHeight);
        }
        int gradingFbWidthInt = 0;
        int gradingFbHeightInt = 0;
        if (gradingDisplay)
        {
            glfwGetFramebufferSize(gradingDisplay->window, &gradingFbWidthInt, &gradingFbHeightInt);
        }
        uint32_t prevGradingFbW = gradingFbWidth;
        uint32_t prevGradingFbH = gradingFbHeight;
        if (cropDisplay)
        {
            glfwGetWindowSize(cropDisplay->window, &cropDisplay->width, &cropDisplay->height);
        }
        if (gradingDisplay)
        {
            glfwGetWindowSize(gradingDisplay->window, &gradingDisplay->width, &gradingDisplay->height);
        }
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

        const bool regionClosed = cropDisplay ? cropDisplay->shouldClose() : false;
        const bool gradingClosed = gradingDisplay ? gradingDisplay->shouldClose() : false;
        if ((display && display->shouldClose()) || regionClosed || gradingClosed)
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
        if (display && cursorInPlayButton(cursorX, cursorY, display->width, display->height))
        {
            playing = !playing;
        }
            else if (display && cursorInScrubber(cursorX, cursorY, display->width, display->height))
            {
                scrubDragging = true;
                scrubDragStartX = cursorX;
                scrubDragStartProgress = static_cast<float>(scrubProgressUi);
                wasPlayingBeforeScrub = playing;
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
            ScrubberUi ui = computeScrubberUi(display->width, display->height);
            double scrubberWidth = std::max(1.0, ui.right - ui.left);
            float deltaProgress = static_cast<float>(deltaX / scrubberWidth);
            float newProgress = std::clamp(scrubDragStartProgress + deltaProgress, 0.0f, 1.0f);
            scrubProgressUi = newProgress;
        }
        else if (mouseState == GLFW_RELEASE && scrubDragging)
        {
            scrubDragging = false;
            const bool resumePlayback = wasPlayingBeforeScrub;
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
            playing = resumePlayback;
        }
        mouseHeld = (mouseState == GLFW_PRESS);

        if (!gradingDisplay)
        {
            gradingMouseHeld = false;
            gradingRightHeld = false;
        }
        else
        {
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
                if (relX >= gradingLayout.loadX0 && relX <= gradingLayout.loadX1 &&
                    relY >= gradingLayout.loadY0 && relY <= gradingLayout.loadY1)
                {
                    return -3; // load button
                }
                if (relX >= gradingLayout.saveX0 && relX <= gradingLayout.saveX1 &&
                    relY >= gradingLayout.saveY0 && relY <= gradingLayout.saveY1)
                {
                    return -4; // save button
                }
                if (relX >= gradingLayout.previewX0 && relX <= gradingLayout.previewX1 &&
                    relY >= gradingLayout.previewY0 && relY <= gradingLayout.previewY1)
                {
                    return -5; // preview toggle
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

            bool loadRequested = false;
            bool saveRequested = false;
            bool previewToggle = false;
            bool detectionToggle = false;
            const bool rightClick = (gradingMouseState == GLFW_PRESS &&
                                     glfwGetMouseButton(gradingDisplay->window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS);
            if (grading::handleOverlayClick(gradingLayout,
                                            gx,
                                            gy,
                                            gradingSettings,
                                            doubleClick,
                                            rightClick,
                                            &loadRequested,
                                            &saveRequested,
                                            &previewToggle,
                                            &detectionToggle))
            {
                gradingOverlayDirty = true;
                curveDirty = true;
                if (loadRequested)
                {
                    if (!grading::loadGradingSettings(gradingConfigPath, gradingSettings))
                    {
                        std::cerr << "[Video2D] Failed to load grading settings from " << gradingConfigPath << "\n";
                    }
                }
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
                    curveDirty = true;
                }
                if (detectionToggle)
                {
                    detectionEnabled = !detectionEnabled;
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
            bool loadRequested = false;
            bool saveRequested = false;
            bool previewToggle = false;
            bool detectionToggle = false;
            if (grading::handleOverlayClick(gradingLayout,
                                            gx,
                                            gy,
                                            gradingSettings,
                                            false,
                                            /*rightClick=*/false,
                                            &loadRequested,
                                            &saveRequested,
                                            &previewToggle,
                                            &detectionToggle))
            {
                gradingOverlayDirty = true;
                curveDirty = true;
                if (loadRequested)
                {
                    if (!grading::loadGradingSettings(gradingConfigPath, gradingSettings))
                    {
                        std::cerr << "[Video2D] Failed to load grading settings from " << gradingConfigPath << "\n";
                    }
                }
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
                if (detectionToggle)
                {
                    detectionEnabled = !detectionEnabled;
                }
            }
        }
        else if (gradingMouseState == GLFW_RELEASE)
        {
            gradingMouseHeld = false;
        }

        int gradingRightState = glfwGetMouseButton(gradingDisplay->window, GLFW_MOUSE_BUTTON_RIGHT);
        if (gradingRightState == GLFW_PRESS && !gradingRightHeld)
        {
            double gx = 0.0;
            double gy = 0.0;
            glfwGetCursorPos(gradingDisplay->window, &gx, &gy);
            bool loadRequested = false;
            bool saveRequested = false;
            bool previewToggle = false;
            bool detectionToggle = false;
            if (grading::handleOverlayClick(gradingLayout,
                                            gx,
                                            gy,
                                            gradingSettings,
                                            /*doubleClick=*/false,
                                            /*rightClick=*/true,
                                            &loadRequested,
                                            &saveRequested,
                                            &previewToggle,
                                            &detectionToggle))
            {
                gradingOverlayDirty = true;
                curveDirty = true;
                if (loadRequested)
                {
                    if (!grading::loadGradingSettings(gradingConfigPath, gradingSettings))
                    {
                        std::cerr << "[Video2D] Failed to load grading settings from " << gradingConfigPath << "\n";
                    }
                }
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
                if (detectionToggle)
                {
                    detectionEnabled = !detectionEnabled;
                }
            }
            gradingRightHeld = true;
        }
        else if (gradingRightState == GLFW_RELEASE)
        {
            gradingRightHeld = false;
        }
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

        double playbackSeconds = advancePlayback(playbackState, display ? (playing && !scrubDragging) : false);
        double normalizedProgress = videoDurationSeconds > 0.0
                                        ? std::clamp(playbackSeconds / videoDurationSeconds, 0.0, 1.0)
                                        : 0.0;
        if (!scrubDragging)
        {
            scrubProgressUi = normalizedProgress;
        }
        const float displayProgress = static_cast<float>(scrubProgressUi);

        ColorAdjustments adjustments{};
        if (gradingPreviewEnabled)
        {
            adjustments.exposure = gradingSettings.exposure;
            adjustments.contrast = gradingSettings.contrast;
            adjustments.saturation = gradingSettings.saturation;
            adjustments.shadows = gradingSettings.shadows;
            adjustments.midtones = gradingSettings.midtones;
            adjustments.highlights = gradingSettings.highlights;
            if (curveDirty)
            {
                grading::buildCurveLut(gradingSettings, curveLut);
                curveDirty = false;
            }
            adjustments.curveLut = curveLut;
            adjustments.curveEnabled = true;
        }
        const ColorAdjustments* activeAdjustments = gradingPreviewEnabled ? &adjustments : nullptr;

        // Rebuild grading overlay if needed or size changed
        if (gradingDisplay && gradingOverlayDirty)
        {
            gradingOverlayDirty = grading::buildGradingOverlay(engine,
                                                               gradingSettings,
                                                               gradingOverlayImage,
                                                               gradingOverlayInfo,
                                                               gradingFbWidth,
                                                               gradingFbHeight,
                                                               gradingLayout,
                                                               gradingPreviewEnabled,
                                                               detectionEnabled);
            gradingOverlayInfo.overlay.sampler = playbackState.overlay.sampler;
        }
        if (display)
        {
            display->renderFrame(playbackState.video.descriptors,
                                 playbackState.overlay.info,
                                 playbackState.fpsOverlay.info,
                                 playbackState.colorInfo,
                                 displayProgress,
                                 playing ? 1.0f : 0.0f,
                                 nullptr,
                                 activeAdjustments);
        }
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
                if (cropDisplay)
                {
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
        }

        // Grading preview window with sliders overlay (no video)
        OverlayImageInfo gradingOverlay = gradingOverlayInfo;
        gradingOverlay.overlay.sampler = playbackState.overlay.sampler;
        OverlayImageInfo disabledFps{};
        RenderOverrides gradingOverrides{};
        gradingOverrides.hideScrubber = true;
        if (gradingDisplay)
        {
            gradingDisplay->renderFrame(blackVideo,
                                        gradingOverlay,
                                        disabledFps,
                                        playbackState.colorInfo,
                                        0.0f,
                                        0.0f,
                                        &gradingOverrides,
                                        activeAdjustments);
        }
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
