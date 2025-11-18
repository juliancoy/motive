#include <iostream>
#include <vector>
#include <memory>
#include <unistd.h> // for sleep
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "engine.h"
#include "display.h"
#include "camera.h"
#include "model.h"

int main(int argc, char *argv[])
{

    // Parse command line arguments
    bool loadTriangle = true;
    bool loadGLTF = false;

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
    }

    // Create engine instance
    Engine *engine = new Engine();

    // Create the primary display/window before entering the render loop
    Display* display = engine->createWindow(800, 600, "Motive");

    // Create a default camera tied to this display
    glm::vec3 defaultCameraPos(0.0f, 0.0f, -3.0f);
    glm::vec2 defaultCameraRotation(glm::radians(-180.0f), 0.0f);
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
