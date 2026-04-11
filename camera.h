#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vulkan/vulkan.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "camera_follow_settings.h"
#include "orbit_camera_rig.h"

// Forward declarations
class Engine;
class Display;
class Model;  // For character controller target and follow mode

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
    void clearInputState();
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

    // Cached camera matrices
    glm::mat4 viewMatrix = glm::mat4(1.0f);
    glm::mat4 projectionMatrix = glm::mat4(1.0f);

    // Getters for camera matrices
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix() const;

    // Descriptor set management
    void allocateDescriptorSet();
    void setOrthographicProjection(float width, float height, float nearPlane = 0.1f, float farPlane = 100.0f);
    void setPerspectiveProjection();
    void setControlsEnabled(bool enabled);
    
    // Character controller target (WASD moves this model instead of camera)
    void setCharacterTarget(Model* model);
    Model* getCharacterTarget() const { return characterTarget; }
    
    // Follow mode - camera tracks a target model by scene index
    // The camera stores the scene index and looks up the model each frame,
    // so it continues to work even if models are reloaded
    void setFollowTarget(int sceneIndex, const FollowSettings& settings = FollowSettings());
    int getFollowTargetIndex() const { return orbitRig.sceneIndex(); }
    void setFollowSettings(const FollowSettings& settings);
    const FollowSettings& getFollowSettings() const { return orbitRig.settings(); }
    void updateFollow(float deltaTime, const std::vector<std::unique_ptr<Model>>& models);  // Update camera position based on follow target
    bool isFollowModeEnabled() const { return orbitRig.isEnabled(); }
    
    // Get the scene index this camera is following (returns -1 if not following)
    int getFollowSceneIndex() const { return orbitRig.sceneIndex(); }
    
    // Camera identification
    void setCameraName(const std::string& name) { cameraName = name; }
    const std::string& getCameraName() const { return cameraName; }
    void setCameraId(const std::string& id) { cameraId = id; }
    const std::string& getCameraId() const { return cameraId; }

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
    float orthoWidth = 10.0f;
    float orthoHeight = 10.0f;
    float orthoNear = 0.1f;
    float orthoFar = 100.0f;
    bool controlsEnabled = true;
    bool fullscreenViewportEnabled = false;
    float fullscreenPercentX = 1.0f;
    float fullscreenPercentY = 1.0f;
    
    Model* characterTarget = nullptr;  // If set, WASD controls this character instead of camera
    
    std::string cameraName;  // For identifying cameras (e.g., "Follow Cam")
    std::string cameraId;
    OrbitCameraRig orbitRig;
};
