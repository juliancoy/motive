#include "engine2d.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <glm/glm.hpp>

namespace motive2d {

Engine2D::Engine2D()
    : engine(nullptr)
    , videoLoaded(false)
    , duration(0.0f)
    , currentTime(0.0f)
    , playing(true)
    , overlayInitialized(false)
    , fpsFrameCounter(0)
    , currentFps(0.0f)
{
    fpsLastSample = std::chrono::steady_clock::now();
}

Engine2D::~Engine2D()
{
    shutdown();
}

bool Engine2D::initialize()
{
    try {
        engine = std::make_unique<Engine>();
        std::cout << "[Engine2D] Engine initialized successfully.\n";
        return true;
    } catch (const std::exception& ex) {
        std::cerr << "[Engine2D] Failed to initialize engine: " << ex.what() << "\n";
        return false;
    }
}

Display2D* Engine2D::createWindow(int width, int height, const char* title)
{
    if (!engine) {
        std::cerr << "[Engine2D] Engine not initialized. Call initialize() first.\n";
        return nullptr;
    }
    
    try {
        Display2D* window = new Display2D(engine.get(), width, height, title);
        windows.push_back(window);
        std::cout << "[Engine2D] Created window: " << title 
                  << " (" << width << "x" << height << ")\n";
        return window;
    } catch (const std::exception& ex) {
        std::cerr << "[Engine2D] Failed to create window: " << ex.what() << "\n";
        return nullptr;
    }
}

bool Engine2D::loadVideo(const std::filesystem::path& filePath, std::optional<bool> swapUV)
{
    if (!engine) {
        std::cerr << "[Engine2D] Engine not initialized.\n";
        return false;
    }
    
    if (!std::filesystem::exists(filePath)) {
        std::cerr << "[Engine2D] Video file not found: " << filePath << "\n";
        return false;
    }
    
    // Initialize video playback
    double durationSeconds = 0.0;
    if (!initializeVideoPlayback(filePath, engine.get(), playbackState, durationSeconds, swapUV)) {
        std::cerr << "[Engine2D] Failed to initialize video playback.\n";
        return false;
    }
    
    videoLoaded = true;
    duration = static_cast<float>(durationSeconds);
    currentTime = 0.0f;
    playing = true;
    
    // Initialize overlay compute
    if (!initializeOverlay()) {
        std::cerr << "[Engine2D] Warning: Failed to initialize overlay compute.\n";
    }
    
    std::cout << "[Engine2D] Loaded video: " << filePath << "\n";
    std::cout << "  Resolution: " << playbackState.decoder.width << "x" << playbackState.decoder.height << "\n";
    std::cout << "  Framerate: " << playbackState.decoder.fps << " fps\n";
    std::cout << "  Duration: " << duration << " seconds\n";
    
    return true;
}

Engine2D::VideoInfo Engine2D::getVideoInfo() const
{
    VideoInfo info{};
    if (videoLoaded) {
        info.width = static_cast<uint32_t>(playbackState.decoder.width);
        info.height = static_cast<uint32_t>(playbackState.decoder.height);
        info.framerate = static_cast<float>(playbackState.decoder.fps);
        info.duration = duration;
    }
    return info;
}

void Engine2D::play()
{
    playing = true;
}

void Engine2D::pause()
{
    playing = false;
}

void Engine2D::seek(float timeSeconds)
{
    if (!videoLoaded) return;
    
    timeSeconds = std::clamp(timeSeconds, 0.0f, duration);
    currentTime = timeSeconds;
    
    // Stop async decoding, seek, then restart
    video::stopAsyncDecoding(playbackState.decoder);
    if (!video::seekVideoDecoder(playbackState.decoder, timeSeconds)) {
        std::cerr << "[Engine2D] Failed to seek video decoder.\n";
    }
    
    // Wait for device idle to avoid destroying in-flight resources
    vkDeviceWaitIdle(engine->logicalDevice);
    
    // Clear pending frames
    playbackState.pendingFrames.clear();
    playbackState.playbackClockInitialized = false;
    
    // Restart async decoding
    video::startAsyncDecoding(playbackState.decoder, 12);
}

void Engine2D::setGrading(const GradingSettings& settings)
{
    gradingSettings = settings;
}

void Engine2D::setCrop(const CropRegion& region)
{
    cropRegion = region;
}

void Engine2D::clearGrading()
{
    gradingSettings.reset();
}

void Engine2D::clearCrop()
{
    cropRegion.reset();
}

bool Engine2D::initializeOverlay()
{
    if (overlayInitialized) return true;
    
    if (!overlay::initializeOverlayCompute(engine.get(), overlayCompute)) {
        return false;
    }
    
    overlayInitialized = true;
    return true;
}

void Engine2D::updateFpsOverlay()
{
    if (!videoLoaded) return;
    
    fpsFrameCounter++;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - fpsLastSample).count();
    
    if (elapsed >= 500) {
        currentFps = static_cast<float>(fpsFrameCounter) * 1000.0f / static_cast<float>(elapsed);
        fpsFrameCounter = 0;
        fpsLastSample = now;
        
        // Update FPS overlay texture
        overlay::updateFpsOverlay(engine.get(),
                                  playbackState.fpsOverlay,
                                  playbackState.overlay.sampler,
                                  playbackState.video.sampler,
                                  currentFps,
                                  playbackState.fpsOverlay.lastRefWidth,
                                  playbackState.fpsOverlay.lastRefHeight);
    }
}

void Engine2D::applyGrading(ColorAdjustments& adjustments) const
{
    if (!gradingSettings) return;
    
    adjustments.exposure = gradingSettings->exposure;
    adjustments.contrast = gradingSettings->contrast;
    adjustments.saturation = gradingSettings->saturation;
    adjustments.shadows = gradingSettings->shadows;
    adjustments.midtones = gradingSettings->midtones;
    adjustments.highlights = gradingSettings->highlights;
    
    if (gradingSettings->curveEnabled) {
        adjustments.curveLut = gradingSettings->curveLut;
        adjustments.curveEnabled = true;
    }
}

RenderOverrides Engine2D::buildRenderOverrides() const
{
    RenderOverrides overrides{};
    
    if (cropRegion) {
        overrides.useCrop = true;
        overrides.cropOrigin = glm::vec2(cropRegion->x, cropRegion->y);
        overrides.cropSize = glm::vec2(cropRegion->width, cropRegion->height);
    }
    
    return overrides;
}

bool Engine2D::renderFrame()
{
    if (!engine || !videoLoaded || windows.empty()) {
        return false;
    }
    
    // Poll events for all windows
    for (auto* window : windows) {
        window->pollEvents();
        if (window->shouldClose()) {
            return false;
        }
    }
    
    // Advance playback
    double playbackSeconds = advancePlayback(playbackState, playing);
    currentTime = static_cast<float>(playbackSeconds);
    
    // Update FPS overlay
    updateFpsOverlay();
    
    // Prepare grading adjustments
    ColorAdjustments adjustments{};
    applyGrading(adjustments);
    const ColorAdjustments* activeAdjustments = gradingSettings ? &adjustments : nullptr;
    
    // Build render overrides
    RenderOverrides overrides = buildRenderOverrides();
    
    // Render to each window
    float scrubProgress = duration > 0.0f ? currentTime / duration : 0.0f;
    float scrubPlaying = playing ? 1.0f : 0.0f;
    
    for (auto* window : windows) {
        window->renderFrame(playbackState.video.descriptors,
                           playbackState.overlay.info,
                           playbackState.fpsOverlay.info,
                           playbackState.colorInfo,
                           scrubProgress,
                           scrubPlaying,
                           &overrides,
                           activeAdjustments);
    }
    
    return true;
}

void Engine2D::run()
{
    if (!engine || !videoLoaded || windows.empty()) {
        std::cerr << "[Engine2D] Cannot run: engine not initialized, no video loaded, or no windows.\n";
        return;
    }
    
    std::cout << "[Engine2D] Starting main render loop...\n";
    
    while (true) {
        if (!renderFrame()) {
            break;
        }
        
        // Small sleep to prevent busy looping
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 Hz
    }
    
    std::cout << "[Engine2D] Render loop ended.\n";
}

void Engine2D::shutdown()
{
    std::cout << "[Engine2D] Shutting down...\n";
    
    // Cleanup overlay compute
    if (overlayInitialized) {
        overlay::destroyOverlayCompute(overlayCompute);
        overlayInitialized = false;
    }
    
    // Cleanup video resources
    if (videoLoaded) {
        video::stopAsyncDecoding(playbackState.decoder);
        
        // Destroy image resources
        overlay::destroyImageResource(engine.get(), playbackState.video.lumaImage);
        overlay::destroyImageResource(engine.get(), playbackState.video.chromaImage);
        overlay::destroyImageResource(engine.get(), playbackState.overlay.image);
        overlay::destroyImageResource(engine.get(), playbackState.fpsOverlay.image);
        
        // Destroy samplers
        if (playbackState.video.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(engine->logicalDevice, playbackState.video.sampler, nullptr);
            playbackState.video.sampler = VK_NULL_HANDLE;
        }
        if (playbackState.overlay.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(engine->logicalDevice, playbackState.overlay.sampler, nullptr);
            playbackState.overlay.sampler = VK_NULL_HANDLE;
        }
        
        video::cleanupVideoDecoder(playbackState.decoder);
        videoLoaded = false;
    }
    
    // Destroy windows
    for (auto* window : windows) {
        if (window) {
            window->shutdown();
            delete window;
        }
    }
    windows.clear();
    
    // Destroy engine
    engine.reset();
    
    std::cout << "[Engine2D] Shutdown complete.\n";
}

} // namespace motive2d
