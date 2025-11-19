#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <memory>
#include <unistd.h> // for sleep
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

int main(int argc, char *argv[])
{

    // Parse command line arguments
    bool loadTriangle = true;
    bool loadGLTF = false;

    bool msaaOverride = false;
    VkSampleCountFlagBits requestedMsaa = VK_SAMPLE_COUNT_1_BIT;

    for (int i = 1; i < argc; ++i)
    {
        if (std::string(argv[i]) == "--triangle")
        {
            loadTriangle = true;
        }
        else if (std::string(argv[i]) == "--gltf")
        {
            loadTriangle = false;
            loadGLTF = true;
        }
        else if (std::string(argv[i]).rfind("--msaa=", 0) == 0)
        {
            std::string value = std::string(argv[i]).substr(7);
            try
            {
                int samples = std::stoi(value);
                VkSampleCountFlagBits flag = msaaFlagFromInt(samples);
                if (flag == static_cast<VkSampleCountFlagBits>(VK_SAMPLE_COUNT_FLAG_BITS_MAX_ENUM))
                {
                    std::cerr << "[Warning] Unsupported MSAA value '" << samples << "'. Valid values: 1,2,4,8,16,32,64." << std::endl;
                }
                else
                {
                    requestedMsaa = flag;
                    msaaOverride = true;
                }
            }
            catch (const std::exception &e)
            {
                std::cerr << "[Warning] Failed to parse MSAA option '" << value << "': " << e.what() << std::endl;
            }
        }
        else if (std::string(argv[i]) == "--video")
        {
            videoMode = true;
        }
    }

    if (videoMode)
    {
        return runVideoPlayback(msaaOverride, requestedMsaa);
    }

    // Create engine instance
    Engine *engine = new Engine();
    if (msaaOverride)
    {
        engine->setMsaaSampleCount(requestedMsaa);
        std::cout << "[Info] Requested MSAA " << msaaIntFromFlag(requestedMsaa)
                  << "x. Using " << msaaIntFromFlag(engine->getMsaaSampleCount()) << "x.\n";
    }

    // Create the primary display/window before entering the render loop
    Display* display = engine->createWindow(800, 600, "Motive");

    // Define primary light for the scene
    Light sceneLight(glm::vec3(0.0f, 0.0f, 1.0f),
                     glm::vec3(0.1f),
                     glm::vec3(0.9f));
    sceneLight.setDiffuse(glm::vec3(1.0f, 0.95f, 0.9f));
    engine->setLight(sceneLight);

    // Create a default camera tied to this display
    glm::vec3 defaultCameraPos(0.0f, 0.0f, 3.0f);
    glm::vec2 defaultCameraRotation(glm::radians(0.0f), 0.0f);
    auto* primaryCamera = new Camera(engine, display, defaultCameraPos, defaultCameraRotation);
    display->addCamera(primaryCamera);

    // Load geometry based on flags
    if (loadTriangle)
    {

        // Triangle vertices with position, normal, and texCoord
        std::vector<Vertex> triangleVerts = {
            {{-0.5f, -0.5f, 0.0f}, {0, 0, 1}, {0.0f, 0.0f}},
            {{0.5f, -0.5f, 0.0f}, {0, 0, 1}, {1.0f, 0.0f}},
            {{0.0f, 0.5f, 0.0f}, {0, 0, 1}, {0.5f, 1.0f}}};

        std::cout << "Triangle vertex count: " << triangleVerts.size() << std::endl;
        for (const auto &v : triangleVerts)
        {
            std::cout << "pos: (" << v.pos.x << ", " << v.pos.y << ", " << v.pos.z << "), "
                      << "normal: (" << v.normal.x << ", " << v.normal.y << ", " << v.normal.z << "), "
                      << "texCoord: (" << v.texCoord.x << ", " << v.texCoord.y << ")\n";
        }

        engine->addModel(std::make_unique<Model>(triangleVerts, engine));
    }
    else if (loadGLTF)
    {
        auto model = std::make_unique<Model>("the_utah_teapot.glb", engine);
        model->scaleToUnitBox();
        model->rotate(-90.0f, 0.0f, 0.0f);
        model->translate(glm::vec3(0.0f, -0.5f, 0.0f));
        engine->addModel(std::move(model));
    }

    engine->renderLoop();

    // Cleanup
    delete engine;

    return 0;
}
