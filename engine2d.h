#pragma once

#include <memory>
#include <string>
#include <filesystem>
#include <optional>
#include <chrono>
#include <array>
#include "engine.h"
#include "display2d.h"
#include "video.h"
#include "decode.h"
#include "overlay.hpp"
#include "grading.hpp"

namespace motive2d {

struct GradingSettings {
    float exposure = 0.0f;
    float contrast = 1.0f;
    float saturation = 1.0f;
    glm::vec3 shadows{1.0f};
    glm::vec3 midtones{1.0f};
    glm::vec3 highlights{1.0f};
    std::array<float, 256> curveLut{};
    bool curveEnabled = false;
};

struct CropRegion {
    float x = 0.0f;  // normalized 0-1
    float y = 0.0f;  // normalized 0-1
    float width = 1.0f;  // normalized 0-1
    float height = 1.0f; // normalized 0-1
};

struct RenderOptions {
    bool showScrubber = true;
    bool showFps = true;
    bool showOverlay = true;
    std::optional<GradingSettings> grading;
    std::optional<CropRegion> crop;
    float playbackSpeed = 1.0f;
};

class Engine2D {
public:
    Engine2D();
    ~Engine2D();

    // Disable copy
    Engine2D(const Engine2D&) = delete;
    Engine2D& operator=(const Engine2D&) = delete;

    // Initialize the engine (Vulkan, GLFW, etc.)
    bool initialize();

    // Create a 2D display window
    Display2D* createWindow(int width = 1280, int height = 720, 
                            const char* title = "Motive 2D");

    // Load a video file
    bool loadVideo(const std::filesystem::path& filePath,
                   std::optional<bool> swapUV = std::nullopt);

    // Get video information
    struct VideoInfo {
        uint32_t width = 0;
        uint32_t height = 0;
        float framerate = 0.0f;
        float duration = 0.0f;
        // Codec info can be derived from decoder
    };
    VideoInfo getVideoInfo() const;

    // Playback control
    void play();
    void pause();
    bool isPlaying() const { return playing; }
    void seek(float timeSeconds);
    float getCurrentTime() const { return currentTime; }
    float getDuration() const { return duration; }

    // Grading and crop
    void setGrading(const GradingSettings& settings);
    void setCrop(const CropRegion& region);
    void clearGrading();
    void clearCrop();

    // Render a frame to all windows
    bool renderFrame();

    // Main loop (blocks until all windows closed)
    void run();

    // Cleanup
    void shutdown();

    // Access underlying engine (for advanced use)
    Engine* getEngine() { return engine.get(); }
    const Engine* getEngine() const { return engine.get(); }

private:
    std::unique_ptr<Engine> engine;
    std::vector<Display2D*> windows;
    
    // Video state
    VideoPlaybackState playbackState;
    bool videoLoaded = false;
    float duration = 0.0f;
    float currentTime = 0.0f;
    bool playing = true;
    
    // Grading and crop state
    std::optional<GradingSettings> gradingSettings;
    std::optional<CropRegion> cropRegion;
    
    // Overlay resources
    overlay::OverlayCompute overlayCompute;
    bool overlayInitialized = false;
    
    // FPS tracking
    std::chrono::steady_clock::time_point fpsLastSample;
    int fpsFrameCounter = 0;
    float currentFps = 0.0f;
    
    // Internal methods
    bool initializeOverlay();
    void updateFpsOverlay();
    void applyGrading(ColorAdjustments& adjustments) const;
    RenderOverrides buildRenderOverrides() const;
};

} // namespace motive2d
