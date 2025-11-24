#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <algorithm>
#include <vector>
#include <chrono>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#include "engine.h"
#include "display.h"
#include "camera.h"
#include "model.h"
#include "utils.h"
#include "light.h"
#include "video.h"
#include "glyph.h"

namespace
{
    const std::filesystem::path kVideoPath = std::filesystem::path("..") / "P1090533.MOV";

    struct CommandLineOptions
    {
        bool loadGltf = false;
        bool testDecode = false;
        std::filesystem::path gltfPath;
    };

    CommandLineOptions parseCommandLineArgs(int argc, char *argv[])
    {
        CommandLineOptions options{};
        const std::string gltfFlag = "--gltf";
        const std::string gltfEqualsPrefix = "--gltf=";

        for (int i = 1; i < argc; ++i)
        {
            std::string arg = argv[i];
            if (arg == gltfFlag)
            {
                options.loadGltf = true;
                if (i + 1 < argc && argv[i + 1][0] != '-')
                {
                    options.gltfPath = argv[++i];
                }
            }
            else if (arg == "--test-decode")
            {
                options.testDecode = true;
            }
            else if (arg.rfind(gltfEqualsPrefix, 0) == 0)
            {
                options.loadGltf = true;
                options.gltfPath = arg.substr(gltfEqualsPrefix.size());
            }
        }

        return options;
    }

    struct VideoPlaybackState {
        video::VideoDecoder decoder;
        Primitive* videoPrimitive = nullptr;
        std::vector<uint8_t> frameBuffer;
        std::chrono::steady_clock::time_point lastFrameTime;
        std::chrono::duration<double> frameDuration;
        bool initialized = false;
        video::VideoColorInfo colorInfo;
        video::Nv12Overlay fpsOverlay;
        bool overlayValid = false;
        size_t framesSinceOverlayUpdate = 0;
        std::chrono::steady_clock::time_point overlayTimerStart;
    };

    std::unique_ptr<VideoPlaybackState> videoState;

    bool runDecodeBenchmark(const std::filesystem::path& videoPath)
    {
        if (!std::filesystem::exists(videoPath))
        {
            std::cerr << "[Video] Benchmark file not found: " << videoPath << std::endl;
            return false;
        }

        video::VideoDecoder decoder;
        if (!video::initializeVideoDecoder(videoPath, decoder))
        {
            std::cerr << "[Video] Failed to initialize decoder for benchmark" << std::endl;
            return false;
        }

        const double targetSeconds = 10.0;
        const double fps = decoder.fps > 0.0 ? decoder.fps : 30.0;
        const size_t targetFrames = static_cast<size_t>(fps * targetSeconds);
        std::vector<uint8_t> frameBuffer;
        size_t framesDecoded = 0;
        auto start = std::chrono::steady_clock::now();

        while (framesDecoded < targetFrames)
        {
            if (!video::decodeNextFrame(decoder, frameBuffer))
            {
                break;
            }
            ++framesDecoded;
        }

        auto end = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsed = end - start;
        const double elapsedSeconds = elapsed.count();
        const double decodeFps = elapsedSeconds > 0.0 ? static_cast<double>(framesDecoded) / elapsedSeconds : 0.0;
        const double videoSecondsDecoded = fps > 0.0 ? static_cast<double>(framesDecoded) / fps : 0.0;

        std::cout << "[Video] Benchmark decoded " << framesDecoded << " frames ("
                  << videoSecondsDecoded << "s of video) in " << elapsedSeconds
                  << "s => " << decodeFps << " FPS" << std::endl;

        if (framesDecoded < targetFrames)
        {
            std::cout << "[Video] File ended before full benchmark duration." << std::endl;
        }

        video::cleanupVideoDecoder(decoder);
        return framesDecoded > 0;
    }

    void updateVideoOverlay(VideoPlaybackState& state, float fps)
    {
        if (state.decoder.width == 0 || state.decoder.height == 0)
        {
            return;
        }
        glyph::OverlayBitmap bitmap = glyph::buildLabeledOverlay(
            static_cast<uint32_t>(state.decoder.width),
            static_cast<uint32_t>(state.decoder.height),
            "VID",
            fps);
        state.fpsOverlay = video::convertOverlayToNv12(bitmap, state.colorInfo);
        state.overlayValid = state.fpsOverlay.isValid();
    }

    void updateVideoFrame()
    {
        if (!videoState || !videoState->initialized || !videoState->videoPrimitive)
        {
            return;
        }

        auto currentTime = std::chrono::steady_clock::now();
        auto elapsed = currentTime - videoState->lastFrameTime;

        // Decode and update frame if enough time has passed
        if (elapsed >= videoState->frameDuration)
        {
            if (video::acquireDecodedFrame(videoState->decoder, videoState->frameBuffer))
            {
                videoState->framesSinceOverlayUpdate++;
                auto overlayNow = std::chrono::steady_clock::now();
                auto overlayElapsed = overlayNow - videoState->overlayTimerStart;
                if (overlayElapsed >= std::chrono::milliseconds(250))
                {
                    const double seconds = std::chrono::duration<double>(overlayElapsed).count();
                    const float fpsValue = seconds > 0.0 ?
                        static_cast<float>(videoState->framesSinceOverlayUpdate / seconds) : 0.0f;
                    updateVideoOverlay(*videoState, fpsValue);
                    videoState->framesSinceOverlayUpdate = 0;
                    videoState->overlayTimerStart = overlayNow;
                }
                if (videoState->overlayValid)
                {
                    video::applyNv12Overlay(videoState->frameBuffer,
                                            videoState->decoder.width,
                                            videoState->decoder.height,
                                            videoState->fpsOverlay);
                }

                videoState->videoPrimitive->updateTextureFromNV12(
                    videoState->frameBuffer.data(),
                    videoState->frameBuffer.size(),
                    videoState->decoder.width,
                    videoState->decoder.height
                );
                videoState->lastFrameTime = currentTime;
            }
            else if (videoState->decoder.finished.load() && !videoState->decoder.threadRunning.load())
            {
                std::cout << "[Video] End of video reached" << std::endl;
                videoState->initialized = false;
            }
        }
    }

    void cleanupVideoPlayback()
    {
        if (videoState)
        {
            video::cleanupVideoDecoder(videoState->decoder);
            videoState.reset();
        }
    }
}

int main(int argc, char *argv[])
{
    const auto options = parseCommandLineArgs(argc, argv);
    if (options.testDecode)
    {
        return runDecodeBenchmark(kVideoPath) ? 0 : 1;
    }
    Engine *engine = new Engine();

    Display *display = engine->createWindow(1280, 720, "FFmpeg Video Preview");

    Light sceneLight(glm::vec3(0.0f, 0.0f, 1.0f),
                     glm::vec3(0.1f),
                     glm::vec3(0.9f));
    sceneLight.setDiffuse(glm::vec3(1.0f, 0.95f, 0.9f));
    engine->setLight(sceneLight);

    glm::vec3 defaultCameraPos(0.0f, 0.0f, 3.0f);
    glm::vec2 defaultCameraRotation(glm::radians(0.0f), 0.0f);
    auto *primaryCamera = new Camera(engine, display, defaultCameraPos, defaultCameraRotation);
    display->addCamera(primaryCamera);

    if (options.loadGltf)
    {
        std::filesystem::path gltfPath = options.gltfPath.empty() ? std::filesystem::path("the_utah_teapot.glb") : options.gltfPath;
        if (!std::filesystem::exists(gltfPath))
        {
            std::cerr << "[GLTF] File not found: " << gltfPath << std::endl;
            delete engine;
            return 1;
        }

        try
        {
            auto gltfModel = std::make_unique<Model>(gltfPath.string(), engine);
            gltfModel->resizeToUnitBox();
            gltfModel->rotate(-90.0f, 0.0f, 0.0f); // Adjust orientation if needed
            engine->addModel(std::move(gltfModel));
        }
        catch (const std::exception &ex)
        {
            std::cerr << "[GLTF] Failed to load " << gltfPath << ": " << ex.what() << std::endl;
            delete engine;
            return 1;
        }
    }
    else
    {
        primaryCamera->cameraPos = glm::vec3(0.0f, 0.0f, 3.0f);
        if (!std::filesystem::exists(kVideoPath))
        {
            std::cerr << "[Video] Hardcoded path " << kVideoPath << " does not exist." << std::endl;
            return false;
        }

        videoState = std::make_unique<VideoPlaybackState>();

        if (!video::initializeVideoDecoder(kVideoPath, videoState->decoder))
        {
            std::cerr << "[Video] Failed to initialize video decoder" << std::endl;
            videoState.reset();
            return false;
        }

        std::cout << "[Video] Video dimensions: " << videoState->decoder.width << "x" << videoState->decoder.height 
                  << ", FPS: " << videoState->decoder.fps << std::endl;

        // Create video quad geometry
        auto quadVertices = video::buildVideoQuadVertices(
            static_cast<float>(videoState->decoder.width), 
            static_cast<float>(videoState->decoder.height)
        );
        
        auto videoModel = std::make_unique<Model>(quadVertices, engine);
        
        // Get the primitive that will display the video
        if (videoModel->meshes.empty() || videoModel->meshes[0].primitives.empty())
        {
            std::cerr << "[Video] Failed to construct quad primitive." << std::endl;
            video::cleanupVideoDecoder(videoState->decoder);
            videoState.reset();
            return false;
        }

        videoState->videoPrimitive = videoModel->meshes[0].primitives[0].get();
        videoState->colorInfo = video::deriveVideoColorInfo(videoState->decoder);
        videoState->videoPrimitive->setYuvColorMetadata(
            static_cast<uint32_t>(videoState->colorInfo.colorSpace),
            static_cast<uint32_t>(videoState->colorInfo.colorRange));
        videoState->videoPrimitive->enableTextureDoubleBuffering();

        // Enable instancing so we can render a 4x4 grid of quads with the same video texture.
        const uint32_t gridDimension = 4;
        const uint32_t totalInstances = gridDimension * gridDimension;
        const float longestVideoSide = static_cast<float>(std::max(videoState->decoder.width, videoState->decoder.height));
        const float normalizedWidth = static_cast<float>(videoState->decoder.width) / longestVideoSide;
        const float normalizedHeight = static_cast<float>(videoState->decoder.height) / longestVideoSide;
        const float stackedGap = 0.0f;
        const float columnSpacing = normalizedWidth + stackedGap;
        const float rowSpacing = normalizedHeight + stackedGap;
        const float colCenter = (static_cast<float>(gridDimension) - 1.0f) * 0.5f;
        const float rowCenter = (static_cast<float>(gridDimension) - 1.0f) * 0.5f;

        videoState->videoPrimitive->instanceCount = totalInstances;
        videoState->videoPrimitive->instanceOffsets.fill(glm::vec3(0.0f));

        uint32_t instanceIndex = 0;
        for (uint32_t row = 0; row < gridDimension; ++row)
        {
            for (uint32_t col = 0; col < gridDimension; ++col)
            {
                if (instanceIndex >= videoState->videoPrimitive->instanceOffsets.size())
                {
                    break;
                }
                const float xOffset = (static_cast<float>(col) - colCenter) * columnSpacing;
                const float yOffset = (static_cast<float>(row) - rowCenter) * rowSpacing;
                videoState->videoPrimitive->instanceOffsets[instanceIndex++] = glm::vec3(xOffset, yOffset, 0.0f);
            }
        }
        
        // Create initial texture for video (gray NV12 frame)
        std::vector<uint8_t> initialFrame(static_cast<size_t>(videoState->decoder.bufferSize), 128);
        videoState->videoPrimitive->updateTextureFromNV12(
            initialFrame.data(),
            initialFrame.size(),
            videoState->decoder.width,
            videoState->decoder.height
        );

        videoState->videoPrimitive->transform = glm::mat4(1.0f);
        videoModel->resizeToUnitBox();
        engine->addModel(std::move(videoModel));

        if (!video::startAsyncDecoding(videoState->decoder, 12))
        {
            std::cerr << "[Video] Failed to start async decoder" << std::endl;
            video::cleanupVideoDecoder(videoState->decoder);
            videoState.reset();
            return false;
        }

        // Set up timing for video playback
        videoState->frameDuration = std::chrono::duration<double>(1.0 / videoState->decoder.fps);
        videoState->lastFrameTime = std::chrono::steady_clock::now();
        videoState->initialized = true;
        videoState->overlayTimerStart = std::chrono::steady_clock::now();
        videoState->framesSinceOverlayUpdate = 0;
        updateVideoOverlay(*videoState, 0.0f);

        std::cout << "[Video] Video playback initialized successfully" << std::endl;
    }

    // Custom render loop for video playback
    for (auto &display : engine->displays)
    {
        while (!glfwWindowShouldClose(display->window))
        {
            updateVideoFrame();
            display->render();
        }
    }

    cleanupVideoPlayback();
    delete engine;
    return 0;
}
