#include <iostream>
#include <vector>
#include <unistd.h> // for sleep
#include "engine.h"

int main(int argc, char* argv[]) {
    // Triangle vertices with position, normal, and texCoord
    std::vector<Vertex> triangleVerts = {
        { {-0.5f, -0.5f, 0.0f}, {0, 0, 1}, {0.0f, 0.0f} },
        { { 0.5f, -0.5f, 0.0f}, {0, 0, 1}, {1.0f, 0.0f} },
        { { 0.0f,  0.5f, 0.0f}, {0, 0, 1}, {0.5f, 1.0f} }
    };
    
    std::cout << "Triangle vertex count: " << triangleVerts.size() << std::endl;
    for (const auto& v : triangleVerts) {
        std::cout << "pos: (" << v.pos.x << ", " << v.pos.y << ", " << v.pos.z << "), "
                << "normal: (" << v.normal.x << ", " << v.normal.y << ", " << v.normal.z << "), "
                << "texCoord: (" << v.texCoord.x << ", " << v.texCoord.y << ")\n";
    }


    // Parse command line arguments
    bool loadTriangle = false;
    bool loadGLTF = false;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--triangle") {
            loadTriangle = true;
        } else if (std::string(argv[i]) == "--gltf") {
            loadGLTF = true;
        }
    }

    // Default to triangle if no flags
    if (!loadTriangle && !loadGLTF) {
        loadTriangle = true;
    }

    // Create engine instance
    Engine* engine = new Engine();

    // Load geometry based on flags
    if (loadTriangle) {
        engine->createVertexBuffer(triangleVerts);
    } else if (loadGLTF) {
        engine->loadFromFile("the_utah_teapot.glb");
    }

    // Main render loop
    while (!glfwWindowShouldClose(engine->window)) {
        engine->render();
    }

    // Cleanup
    delete engine;

    return 0;
}
