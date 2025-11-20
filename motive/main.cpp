#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
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

namespace
{
    const std::filesystem::path kVideoPath = std::filesystem::path("..") / "P1090533.MOV";
    constexpr VkFormat kVideoTextureFormat = VK_FORMAT_R8G8B8A8_UNORM;

    struct CommandLineOptions
    {
        bool loadGltf = false;
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
    };

    std::unique_ptr<VideoPlaybackState> videoState;

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
            if (video::decodeNextFrame(videoState->decoder, videoState->frameBuffer))
            {
                videoState->videoPrimitive->updateTextureFromPixelData(
                    videoState->frameBuffer.data(),
                    videoState->frameBuffer.size(),
                    videoState->decoder.width,
                    videoState->decoder.height,
                    kVideoTextureFormat
                );
            }
            else
            {
                std::cout << "[Video] End of video reached" << std::endl;
                // Optionally restart video or exit
                // For now, just stop updating
                videoState->initialized = false;
            }
            videoState->lastFrameTime = currentTime;
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
        primaryCamera->cameraPos = glm::vec3(0.0f, 0.0f, 1.0f);
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
        
        // Create initial texture for video (gray frame)
        std::vector<uint8_t> initialFrame(videoState->decoder.width * videoState->decoder.height * 4, 128);
        videoState->videoPrimitive->updateTextureFromPixelData(
            initialFrame.data(), 
            initialFrame.size(), 
            videoState->decoder.width, 
            videoState->decoder.height, 
            kVideoTextureFormat
        );

        videoState->videoPrimitive->transform = glm::mat4(1.0f);
        videoModel->resizeToUnitBox();
        engine->addModel(std::move(videoModel));

        // Set up timing for video playback
        videoState->frameDuration = std::chrono::duration<double>(1.0 / videoState->decoder.fps);
        videoState->lastFrameTime = std::chrono::steady_clock::now();
        videoState->initialized = true;

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
