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
    Camera(Engine* engine,
           Display* display,
           const glm::vec3& initialPos = glm::vec3(0.0f, 0.0f, -3.0f),
           const glm::vec2& initialRot = glm::vec2(glm::radians(-180.0f), 0.0f));
    ~Camera();

    // Camera state management
    void update(uint32_t currentImage);
    void reset();
    
    // Input handling
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPos(double xpos, double ypos);
    void handleKey(int key, int scancode, int action, int mods);
    void setWindow(GLFWwindow* window);
    void setViewport(float centerX, float centerY, float viewportWidth, float viewportHeight);
    void setFullscreenViewportEnabled(bool enabled, float xPercent = 1.0f, float yPercent = 1.0f);
    bool isFullscreenViewportEnabled() const;
    float getFullscreenPercentX() const;
    float getFullscreenPercentY() const;

    // Camera state
    glm::vec3 initialCameraPos;
    glm::vec2 initialCameraRotation;
    glm::vec3 cameraPos;
    glm::vec2 cameraRotation;
    float moveSpeed = 0.01f;

    // Input tracking
    bool rightMouseDown = false;
    glm::vec2 lastMousePos = glm::vec2(0.0f);
    bool keysPressed[6] = {false}; // W,A,S,D,Q,E

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
    void setOrthographicProjection(float width, float height, float nearPlane = -1.0f, float farPlane = 1.0f);
    void setPerspectiveProjection();
    void setControlsEnabled(bool enabled);

private:
    Engine* engine;
    Display* display;
    GLFWwindow* windowHandle = nullptr;

    // Helper methods
    void createCameraUBO();
    void destroyCameraUBO();
    void updateCameraMatrices();
    void registerWindowCallbacks();

    bool useOrthoProjection = false;
    float orthoWidth = 2.0f;
    float orthoHeight = 2.0f;
    float orthoNear = -1.0f;
    float orthoFar = 1.0f;
    bool controlsEnabled = true;
    bool fullscreenViewportEnabled = false;
    float fullscreenPercentX = 1.0f;
    float fullscreenPercentY = 1.0f;
};
