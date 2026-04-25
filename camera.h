#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan.h>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "camera_follow_settings.h"
#include "camera_mode.h"
#include "orbit_follow_rig.h"
#include "follow_target_tracker.h"

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
    void setExternalMouseInput(bool enabled) { externalMouseInput = enabled; }
    void setInvertHorizontalDrag(bool enabled) { invertHorizontalDrag = enabled; }
    bool isHorizontalDragInverted() const { return invertHorizontalDrag; }
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
    float moveSpeed = 0.01f;

    glm::vec2 getEulerRotation() const;
    void setEulerRotation(const glm::vec2& rotation);
    glm::vec3 getForwardVector() const;
    const glm::quat& getOrientation() const { return cameraOrientation; }

    // Input tracking
    bool rightMouseDown = false;
    bool dragAnchorValid = false;
    bool externalMouseInput = false;
    bool invertHorizontalDrag = false;
    bool temporaryOrbitDrag = false;
    glm::vec2 lastMousePos = glm::vec2(0.0f);
    bool keysPressed[6] = {false}; // W,A,S,D,Q,E

    // Display reference for accessing InputRouter
    void setDisplay(Display* disp) { display = disp; }

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
    void setPerspectiveNearFar(float nearPlane, float farPlane);
    float getPerspectiveNear() const { return perspNear; }
    float getPerspectiveFar() const { return perspFar; }
    void setControlsEnabled(bool enabled);
    
    // Follow mode - camera tracks a target model by scene index.
    // Explicit contract: setFollowTarget only mutates follow target/settings and
    // does not implicitly change Camera::mode for assign or clear.
    // The camera stores the scene index and looks up the model each frame,
    // so it continues to work even if models are reloaded.
    void setFollowTarget(int sceneIndex, const FollowSettings& settings = FollowSettings());
    int getFollowTargetIndex() const { return followOrbit.sceneIndex(); }
    void setFollowSettings(const FollowSettings& settings);
    const FollowSettings& getFollowSettings() const { return followOrbit.settings(); }
    void updateFollow(float deltaTime, const std::vector<std::unique_ptr<Model>>& models);  // Update camera position based on follow target
    bool isFollowModeEnabled() const { return followOrbit.isEnabled(); }
    bool hasTrackedFollowTarget() const { return followTargetValidForView; }
    const glm::vec3& getTrackedFollowTarget() const { return followTargetForView; }
    const glm::vec3& getTrackedFollowTargetRaw() const { return followTargetRaw; }
    const glm::vec3& getTrackedFollowTargetMotion() const { return followTargetMotion; }
    
    // Get the scene index this camera is following (returns -1 if not following)
    int getFollowSceneIndex() const { return followOrbit.sceneIndex(); }
    
    // Camera identification
    void setCameraName(const std::string& name) { cameraName = name; }
    const std::string& getCameraName() const { return cameraName; }
    void setCameraId(const std::string& id) { cameraId = id; }
    const std::string& getCameraId() const { return cameraId; }
    
    // Camera mode management
    CameraMode getMode() const { return mode; }
    void setMode(CameraMode newMode);
    bool canEnterMode(CameraMode candidateMode) const;
    
    // Get the model being followed (if any)
    Model* getFollowTarget(const std::vector<std::unique_ptr<Model>>& models) const;
    
    // Free fly camera mode (WASD moves camera vs character) - deprecated, use setMode instead
    void setFreeFlyCamera(bool freeFly);
    bool isFreeFlyCamera() const;
    
    // Controls enabled state
    bool getControlsEnabled() const { return controlsEnabled; }

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
    float perspNear = 0.1f;
    float perspFar = 100.0f;
    bool controlsEnabled = true;
    bool fullscreenViewportEnabled = false;
    float fullscreenPercentX = 1.0f;
    float fullscreenPercentY = 1.0f;
    
    void addYawOffset(float deltaYaw);
    void addPitchOffset(float deltaPitch);
    void updateFollowOffsetFromCameraRotation();
    const char* modeName(CameraMode value) const;
    void syncOrientationFromEulerCacheIfNeeded();
    void syncEulerCacheFromOrientation();
    static glm::quat quaternionFromYawPitch(float yaw, float pitch);
    static glm::vec2 yawPitchFromQuaternion(const glm::quat& orientation);
    
    std::string cameraName;  // For identifying cameras (e.g., "Follow Cam")
    std::string cameraId;
    CameraMode mode = CameraMode::FreeFly;  // Current camera mode
    FollowOrbit followOrbit;
    OrbitRig orbitRig;
    bool followWarningActive = false;
    float followWarningCooldownSeconds = 0.0f;
    bool followTargetValidForView = false;
    glm::vec3 followTargetForView = glm::vec3(0.0f);
    glm::vec3 followTargetRaw = glm::vec3(0.0f);
    glm::vec3 followTargetMotion = glm::vec3(0.0f);
    FollowTargetTracker followTargetTracker;
    glm::quat cameraOrientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    // Euler cache (yaw, pitch in radians). Orientation is authoritative.
    glm::vec2 cameraRotation = glm::vec2(0.0f);
    glm::vec2 orientationSyncedEuler = glm::vec2(0.0f);
};
