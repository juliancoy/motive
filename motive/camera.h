#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#include <iostream>

// Forward declarations
class Engine;
class Display;

struct CameraTransform {
    glm::mat4 view;
    glm::mat4 proj;
};

class Camera
{
public:
    Camera(Engine* engine, Display *display);
    ~Camera();

    // Camera state management
    void update(uint32_t currentImage);
    void reset();
    
    // Input handling
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPos(double xpos, double ypos);
    void handleKey(int key, int scancode, int action, int mods);
    void setWindow(GLFWwindow* window);

    // Camera state
    glm::vec3 initialCameraPos = glm::vec3(0.0f, 0.0f, -3.0f);
    glm::vec2 initialCameraRotation = glm::vec2(glm::radians(-180.0f), 0.0f);
    glm::vec3 cameraPos = initialCameraPos;
    glm::vec2 cameraRotation = initialCameraRotation;
    float moveSpeed = 0.01f;

    // Input tracking
    bool rightMouseDown = false;
    glm::vec2 lastMousePos = glm::vec2(0.0f);
    bool keysPressed[5] = {false}; // W,A,S,D

    // Vulkan resources
    VkBuffer cameraTransformUBO;
    VkDeviceMemory cameraTransformDeviceUBO;
    void* camera0TransformMappedUBO = nullptr;
    VkDescriptorSet descriptorSet;

    // Viewport properties
    glm::vec2 centerpoint = glm::vec2(400.0f, 300.0f); // Center of display
    float width = 800.0f;
    float height = 600.0f;

    // Getters for camera matrices
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;

    // Descriptor set management
    void allocateDescriptorSet();

private:
    Engine* engine;
    Display* display;
    GLFWwindow* windowHandle = nullptr;

    // Helper methods
    void createCameraUBO();
    void destroyCameraUBO();
    void updateCameraMatrices();
    void registerWindowCallbacks();
};
