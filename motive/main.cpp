#include <exception>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
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

    bool addVideoQuadModel(Engine *engine)
    {
        if (!std::filesystem::exists(kVideoPath))
        {
            std::cerr << "[Video] Hardcoded path " << kVideoPath << " does not exist." << std::endl;
            return false;
        }

        video::VideoDecoder decoder{};
        if (!video::initializeVideoDecoder(kVideoPath, decoder))
        {
            video::cleanupVideoDecoder(decoder);
            return false;
        }

        std::vector<uint8_t> frameBuffer;
        if (!video::decodeNextFrame(decoder, frameBuffer))
        {
            std::cerr << "[Video] Unable to decode first frame from " << kVideoPath << std::endl;
            video::cleanupVideoDecoder(decoder);
            return false;
        }

        auto quadVertices = video::buildVideoQuadVertices(static_cast<float>(decoder.width), static_cast<float>(decoder.height));
        auto videoModel = std::make_unique<Model>(quadVertices, engine);
        Primitive *videoPrimitive = nullptr;
        if (!videoModel->meshes.empty() && !videoModel->meshes[0].primitives.empty())
        {
            videoPrimitive = videoModel->meshes[0].primitives[0].get();
        }
        if (!videoPrimitive)
        {
            std::cerr << "[Video] Failed to construct quad primitive." << std::endl;
            video::cleanupVideoDecoder(decoder);
            return false;
        }

        videoPrimitive->updateTextureFromPixelData(frameBuffer.data(),
                                                   frameBuffer.size(),
                                                   static_cast<uint32_t>(decoder.width),
                                                   static_cast<uint32_t>(decoder.height),
                                                   kVideoTextureFormat);
        videoPrimitive->transform = glm::mat4(1.0f);
        videoModel->resizeToUnitBox();
        engine->addModel(std::move(videoModel));

        video::cleanupVideoDecoder(decoder);
        return true;
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
    else if (!addVideoQuadModel(engine))
    {
        delete engine;
        return 1;
    }

    engine->renderLoop();

    delete engine;
    return 0;
}
