#include "camera.h"
#include "camera_mode_rules.h"
#include "engine.h"
#include "display.h"
#include "input_router.h"
#include <stdexcept>
#include <iostream>
#include <array>
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtx/quaternion.hpp>

namespace
{
} // namespace

Camera::Camera(Engine *engine,
               Display *display,
               const glm::vec3 &initialPos,
               const glm::vec2 &initialRot)
    : engine(engine),
      display(display),
      initialCameraPos(initialPos),
      initialCameraRotation(initialRot),
      cameraPos(initialPos),
      cameraRotation(initialRot)
{
    // Initialize camera state
    cameraPos = initialCameraPos;
    setEulerRotation(initialCameraRotation);

    // Initialize orthographic defaults based on the initial viewport
    const float initialAspect = (height > 0.0f) ? (width / height) : (800.0f / 600.0f);
    orthoHeight = 10.0f;
    orthoWidth = orthoHeight * initialAspect;
    orthoNear = 0.1f;
    orthoFar = 100.0f;
    std::cout << "[Debug] Camera created at " << this
              << " position (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")"
              << " rotation (" << cameraRotation.x << ", " << cameraRotation.y << ")"
              << std::endl;

    // Create camera UBO
    createCameraUBO();

    // Note: Descriptor set allocation is deferred until after graphics pipeline creation
    // when engine->descriptorSetLayout is available
    descriptorSet = VK_NULL_HANDLE;
    registerWindowCallbacks();
}

Camera::~Camera()
{
    destroyCameraUBO();
}

void Camera::createCameraUBO()
{
    // Create the Camera View/Projection uniform buffer with proper alignment
    VkDeviceSize cameraTransformUBOBufferSize = sizeof(CameraTransform);
    VkBufferCreateInfo cameraTransformBufferInfo{};
    cameraTransformBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    cameraTransformBufferInfo.size = cameraTransformUBOBufferSize;
    cameraTransformBufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    cameraTransformBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(engine->logicalDevice, &cameraTransformBufferInfo, nullptr, &cameraTransformUBO) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create uniform buffer!");
    }

    // Get memory requirements with proper alignment
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(engine->logicalDevice, cameraTransformUBO, &memRequirements);

    // Allocate memory with proper alignment
    VkMemoryAllocateInfo memAllocInfo{};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.allocationSize = memRequirements.size;
    memAllocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits,
                                                          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                                              VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(engine->logicalDevice, &memAllocInfo, nullptr, &cameraTransformDeviceUBO) != VK_SUCCESS)
    {
        vkDestroyBuffer(engine->logicalDevice, cameraTransformUBO, nullptr);
        throw std::runtime_error("Failed to allocate uniform buffer memory!");
    }

    if (vkBindBufferMemory(engine->logicalDevice, cameraTransformUBO, cameraTransformDeviceUBO, 0) != VK_SUCCESS)
    {
        vkFreeMemory(engine->logicalDevice, cameraTransformDeviceUBO, nullptr);
        vkDestroyBuffer(engine->logicalDevice, cameraTransformUBO, nullptr);
        throw std::runtime_error("Failed to bind uniform buffer memory!");
    }

    // Initialize mapped pointer
    std::cout << "About to map uniform buffer" << std::endl;
    if (vkMapMemory(engine->logicalDevice, cameraTransformDeviceUBO, 0, cameraTransformUBOBufferSize, 0, &camera0TransformMappedUBO) != VK_SUCCESS)
    {
        vkFreeMemory(engine->logicalDevice, cameraTransformDeviceUBO, nullptr);
        vkDestroyBuffer(engine->logicalDevice, cameraTransformUBO, nullptr);
        throw std::runtime_error("Failed to map uniform buffer memory!");
    }
}

void Camera::destroyCameraUBO()
{
    if (engine && engine->logicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(engine->logicalDevice);
        if (cameraTransformUBO != VK_NULL_HANDLE)
        {
            vkDestroyBuffer(engine->logicalDevice, cameraTransformUBO, nullptr);
            cameraTransformUBO = VK_NULL_HANDLE;
        }
        if (cameraTransformDeviceUBO != VK_NULL_HANDLE)
        {
            vkFreeMemory(engine->logicalDevice, cameraTransformDeviceUBO, nullptr);
            cameraTransformDeviceUBO = VK_NULL_HANDLE;
        }
    }
}

void Camera::allocateDescriptorSet()
{
    // Allocate descriptor set for this camera
    VkDescriptorSetAllocateInfo descriptorAllocInfo{};
    descriptorAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocInfo.descriptorPool = engine->descriptorPool;
    descriptorAllocInfo.descriptorSetCount = 1;
    descriptorAllocInfo.pSetLayouts = &engine->descriptorSetLayout;

    if (engine->allocateDescriptorSet(engine->descriptorPool, engine->descriptorSetLayout, descriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate camera descriptor set!");
    }
    else
    {
        std::cout << "[Debug] Camera " << this << " descriptor set allocated: " << descriptorSet << std::endl;
    }

    VkDescriptorBufferInfo cameraBufferInfo{};
    cameraBufferInfo.buffer = cameraTransformUBO;
    cameraBufferInfo.offset = 0;
    cameraBufferInfo.range = sizeof(CameraTransform);

    VkDescriptorBufferInfo lightBufferInfo{};
    lightBufferInfo.buffer = engine->getLightBuffer();
    lightBufferInfo.offset = 0;
    lightBufferInfo.range = engine->getLightUBOSize();

    if (lightBufferInfo.buffer == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Light buffer is not initialized.");
    }

    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &cameraBufferInfo;

    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = descriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pBufferInfo = &lightBufferInfo;

    vkUpdateDescriptorSets(engine->logicalDevice,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(),
                           0,
                           nullptr);
}

void Camera::update(uint32_t currentImage)
{
    (void)currentImage;
    // Recover from missed mouse-release events (focus/capture transitions).
    // If orbit drag is active but RMB is no longer physically pressed, end drag.
    if (rightMouseDown && windowHandle && !externalMouseInput)
    {
        const int rmbState = glfwGetMouseButton(windowHandle, GLFW_MOUSE_BUTTON_RIGHT);
        if (rmbState != GLFW_PRESS)
        {
            rightMouseDown = false;
            dragAnchorValid = false;
            if (temporaryOrbitDrag && mode == CameraMode::OrbitFollow)
            {
                setMode(CameraMode::CharacterFollow);
            }
            temporaryOrbitDrag = false;
        }
    }
    updateCameraMatrices();
}

void Camera::reset()
{
    cameraPos = initialCameraPos;
    setEulerRotation(initialCameraRotation);
    std::cout << "[Debug] Camera " << this << " reset. position (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z
              << ") rotation (" << cameraRotation.x << ", " << cameraRotation.y << ")" << std::endl;
}

void Camera::handleMouseButton(int button, int action, int mods)
{
    const bool allowFollowOrbitInput = followOrbit.isEnabled();
    if (!controlsEnabled && !allowFollowOrbitInput)
    {
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        const bool pressed = (action == GLFW_PRESS);
        if (pressed)
        {
            // Orbit drag is a temporary sub-mode over character follow.
            if (followOrbit.isEnabled() && mode == CameraMode::CharacterFollow)
            {
                temporaryOrbitDrag = true;
                setMode(CameraMode::OrbitFollow);
            }
        }
        else
        {
            if (temporaryOrbitDrag && mode == CameraMode::OrbitFollow)
            {
                setMode(CameraMode::CharacterFollow);
            }
            temporaryOrbitDrag = false;
        }
        rightMouseDown = pressed;
        // Anchor drag from the first subsequent cursor sample.
        // This avoids mixing coordinate spaces (GLFW window coords vs Qt widget coords),
        // which can cause jumpy/non-smooth right-drag rotation.
        dragAnchorValid = false;
    }
}

void Camera::handleCursorPos(double xpos, double ypos)
{
    const bool allowFollowOrbitInput = followOrbit.isEnabled();
    if (!controlsEnabled && !allowFollowOrbitInput)
    {
        return;
    }

        if (rightMouseDown)
        {
            glm::vec2 currentPos(xpos, ypos);
            if (!dragAnchorValid)
            {
                lastMousePos = currentPos;
                dragAnchorValid = true;
                return;
            }
            glm::vec2 delta = currentPos - lastMousePos;
            lastMousePos = currentPos;

            const float sensitivity = 0.003f;
            const float horizontalSign = invertHorizontalDrag ? 1.0f : -1.0f;
            float deltaYaw = delta.x * sensitivity * horizontalSign;
            float deltaPitch = -delta.y * sensitivity;
            
            // Clamp pitch
            deltaPitch = std::clamp(deltaPitch, -0.1f, 0.1f);
            
            if (followOrbit.isEnabled() && mode != CameraMode::FreeFly)
            {
                // In follow mode: orbit camera around target
                // Match free-fly horizontal drag direction for consistent feel.
                addYawOffset(deltaYaw);
                addPitchOffset(deltaPitch);
            }
            else
            {
                // Free-fly mode or no target: rotate camera directly
                glm::vec2 rot = getEulerRotation();
                rot.x += deltaYaw;
                rot.y -= delta.y * sensitivity;
                rot.y = std::clamp(rot.y, -followcam::kFreeFlyMaxPitchRadians, followcam::kFreeFlyMaxPitchRadians);
                setEulerRotation(rot);
            }
        }
}

#include "model.h"  // For character controller

namespace {

glm::vec3 followAnchorPosition(const Model& model, const FollowSettings& settings)
{
    return model.getFollowAnchorPosition() + settings.targetOffset;
}

float normalizeAngle(float angle)
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;
    while (angle > kPi) angle -= kTwoPi;
    while (angle < -kPi) angle += kTwoPi;
    return angle;
}

}

void Camera::handleKey(int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_P && action == GLFW_PRESS)
    {
        setPerspectiveProjection();
        return;
    }
    if (key == GLFW_KEY_O && action == GLFW_PRESS)
    {
        setOrthographicProjection(orthoWidth, orthoHeight, orthoNear, orthoFar);
        return;
    }

    if (!controlsEnabled)
    {
        return;
    }

    // Camera only handles camera-specific keys (P/O for projection, R for reset)
    // WASD input is handled exclusively by InputRouter
    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        reset();
    }
}

void Camera::clearInputState()
{
    std::fill(std::begin(keysPressed), std::end(keysPressed), false);
    rightMouseDown = false;
    dragAnchorValid = false;
}

void Camera::setWindow(GLFWwindow *window)
{
    windowHandle = window;
    registerWindowCallbacks();
}

void Camera::setViewport(float centerX, float centerY, float viewportWidth, float viewportHeight)
{
    width = std::max(1.0f, viewportWidth);
    height = std::max(1.0f, viewportHeight);
    centerpoint = glm::vec2(centerX, centerY);
}

void Camera::setFullscreenViewportEnabled(bool enabled, float xPercent, float yPercent)
{
    fullscreenViewportEnabled = enabled;
    if (enabled)
    {
        fullscreenPercentX = std::clamp(xPercent, 0.01f, 1.0f);
        fullscreenPercentY = std::clamp(yPercent, 0.01f, 1.0f);
    }
}

bool Camera::isFullscreenViewportEnabled() const
{
    return fullscreenViewportEnabled;
}

float Camera::getFullscreenPercentX() const
{
    return fullscreenPercentX;
}

float Camera::getFullscreenPercentY() const
{
    return fullscreenPercentY;
}

void Camera::updateCameraMatrices()
{
    CameraTransform camera0TransformUBO{};
    const glm::vec3 front = getForwardVector();
    glm::vec3 up = cameraOrientation * glm::vec3(0.0f, 1.0f, 0.0f);
    if (glm::length(up) <= 1e-6f)
    {
        up = glm::vec3(0.0f, 1.0f, 0.0f);
    }
    else
    {
        up = glm::normalize(up);
    }

    // Camera movement is handled by InputRouter::update()
    // In FreeFly mode: InputRouter moves camera directly via inout_cameraPos parameter
    // In CharacterFollow mode: InputRouter moves character, camera follows via updateFollow()
    // In other modes: No camera movement

    if ((mode == CameraMode::CharacterFollow || mode == CameraMode::OrbitFollow) && followTargetValidForView)
    {
        glm::vec3 toTarget = followTargetForView - cameraPos;
        if (glm::length(toTarget) > 1e-6f)
        {
            const glm::vec3 forward = glm::normalize(toTarget);
            glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(forward, worldUp)) > 0.999f)
            {
                worldUp = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            viewMatrix = glm::lookAt(cameraPos, followTargetForView, worldUp);
        }
        else
        {
            viewMatrix = glm::lookAt(cameraPos, cameraPos + front, up);
        }
    }
    else
    {
        viewMatrix = glm::lookAt(cameraPos, cameraPos + front, up);
    }
    camera0TransformUBO.view = viewMatrix;

    float aspect = (height > 0.0f) ? (width / height) : (800.0f / 600.0f);

    if (useOrthoProjection)
    {
        const float halfWidth = orthoWidth * 0.5f;
        const float halfHeight = orthoHeight * 0.5f;
        projectionMatrix = glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, orthoNear, orthoFar);
    }
    else
    {
        projectionMatrix = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, perspNear, perspFar);
    }
    projectionMatrix[1][1] *= -1;
    camera0TransformUBO.proj = projectionMatrix;

    if (camera0TransformMappedUBO)
    {
        memcpy(camera0TransformMappedUBO, &camera0TransformUBO, sizeof(camera0TransformUBO));
    }
    else
    {
        throw std::runtime_error("Uniform buffer not mapped!");
    }
}

void Camera::registerWindowCallbacks()
{
    if (!windowHandle && display)
    {
        windowHandle = display->window;
    }
}

glm::mat4 Camera::getViewMatrix() const
{
    return viewMatrix;
}

glm::mat4 Camera::getProjectionMatrix() const
{
    return projectionMatrix;
}

void Camera::setOrthographicProjection(float width, float height, float nearPlane, float farPlane)
{
    useOrthoProjection = true;
    orthoWidth = std::max(0.001f, width);
    orthoHeight = std::max(0.001f, height);
    orthoNear = nearPlane;
    const float minDepthSeparation = 0.001f;
    orthoFar = std::max(orthoNear + minDepthSeparation, farPlane);
    std::cout << "[Camera] Orthographic projection enabled (width=" << orthoWidth
              << ", height=" << orthoHeight << ", near=" << orthoNear << ", far=" << orthoFar << ")" << std::endl;
}

void Camera::setPerspectiveProjection()
{
    useOrthoProjection = false;
    std::cout << "[Camera] Perspective projection enabled" << std::endl;
}

void Camera::setPerspectiveNearFar(float nearPlane, float farPlane)
{
    perspNear = std::max(0.001f, nearPlane);
    perspFar = std::max(perspNear + 0.001f, farPlane);
    std::cout << "[Camera] Perspective near/far set: near=" << perspNear << ", far=" << perspFar << std::endl;
}

void Camera::setControlsEnabled(bool enabled)
{
    controlsEnabled = enabled;
    if (!controlsEnabled)
    {
        clearInputState();
    }
}


void Camera::setFollowTarget(int sceneIndex, const FollowSettings& settings)
{
    std::cout << "[Camera][DEBUG] setFollowTarget called: sceneIndex=" << sceneIndex 
              << " current mode=" << static_cast<int>(mode) 
              << " current followIndex=" << followOrbit.sceneIndex() << std::endl;
    
    const FollowSettings sanitized = followcam::sanitizeSettings(settings);
    if (sceneIndex >= 0) {
        followOrbit.configure(sceneIndex, sanitized);
        orbitRig.configure(sceneIndex, sanitized);
        followTargetValidForView = false;
        followTargetRaw = glm::vec3(0.0f);
        followTargetMotion = glm::vec3(0.0f);
        followTargetTracker.reset();
        // Follow target assignment should not implicitly change camera mode.
        // Callers explicitly control mode transitions via setMode().
        clearInputState();
        std::cout << "[Camera] Follow target set: scene index " << sceneIndex << std::endl;
        std::cout << "[Camera] Follow settings: distance=" << sanitized.distance
                  << ", yaw=" << sanitized.relativeYaw
                  << ", pitch=" << sanitized.relativePitch
                  << ", smooth=" << sanitized.smoothSpeed << std::endl;
        if (std::abs(sanitized.distance - settings.distance) > 1e-4f ||
            std::abs(sanitized.relativePitch - settings.relativePitch) > 1e-4f ||
            std::abs(sanitized.smoothSpeed - settings.smoothSpeed) > 1e-4f)
        {
            std::cout << "[Camera][Follow][Sanitized] Requested follow params were adjusted to keep target visible."
                      << " requestedDistance=" << settings.distance
                      << " requestedPitch=" << settings.relativePitch
                      << " requestedSmooth=" << settings.smoothSpeed
                      << " appliedDistance=" << sanitized.distance
                      << " appliedPitch=" << sanitized.relativePitch
                      << " appliedSmooth=" << sanitized.smoothSpeed
                      << std::endl;
        }
    } else {
        followOrbit.clear();
        orbitRig.clear();
        followTargetValidForView = false;
        followTargetRaw = glm::vec3(0.0f);
        followTargetMotion = glm::vec3(0.0f);
        followTargetTracker.reset();
        std::cout << "[Camera] Follow target cleared" << std::endl;
        followWarningActive = false;
        followWarningCooldownSeconds = 0.0f;
    }
}

void Camera::setFollowSettings(const FollowSettings& settings)
{
    const int sceneIndex = followOrbit.sceneIndex();
    if (sceneIndex >= 0 || settings.enabled)
    {
        const FollowSettings sanitized = followcam::sanitizeSettings(settings);
        followOrbit.configure(sceneIndex, sanitized);
        orbitRig.configure(sceneIndex, sanitized);
        if (std::abs(sanitized.distance - settings.distance) > 1e-4f ||
            std::abs(sanitized.relativePitch - settings.relativePitch) > 1e-4f ||
            std::abs(sanitized.smoothSpeed - settings.smoothSpeed) > 1e-4f)
        {
            std::cout << "[Camera][Follow][Sanitized] setFollowSettings adjusted parameters."
                      << " requestedDistance=" << settings.distance
                      << " requestedPitch=" << settings.relativePitch
                      << " requestedSmooth=" << settings.smoothSpeed
                      << " appliedDistance=" << sanitized.distance
                      << " appliedPitch=" << sanitized.relativePitch
                      << " appliedSmooth=" << sanitized.smoothSpeed
                      << std::endl;
        }
    }
}

void Camera::addYawOffset(float deltaYaw)
{
    FollowSettings settings = followOrbit.settings();
    settings.relativeYaw = normalizeAngle(settings.relativeYaw + deltaYaw);
    followOrbit.configure(followOrbit.sceneIndex(), settings);
    orbitRig.configure(followOrbit.sceneIndex(), settings);
}

void Camera::addPitchOffset(float deltaPitch)
{
    FollowSettings settings = followOrbit.settings();
    settings.relativePitch = std::clamp(settings.relativePitch + deltaPitch, -followcam::kMaxPitchRadians, followcam::kMaxPitchRadians);
    followOrbit.configure(followOrbit.sceneIndex(), settings);
    orbitRig.configure(followOrbit.sceneIndex(), settings);
}

void Camera::updateFollowOffsetFromCameraRotation()
{
    // When user orbits with right-drag, update the follow offset to match
    // This allows camera orbit while maintaining fixed distance from character
    FollowSettings settings = followOrbit.settings();
    
    // The camera's current rotation (cameraRotation) is absolute, we need relative
    // relativeYaw is offset FROM character's facing direction
    // For now, just store absolute and compute relative on each frame
    const glm::vec2 rot = getEulerRotation();
    settings.relativeYaw = rot.x;
    settings.relativePitch = rot.y;
    
    followOrbit.configure(followOrbit.sceneIndex(), settings);
}

void Camera::updateFollow(float deltaTime, const std::vector<std::unique_ptr<Model>>& models)
{
    if (!followOrbit.isEnabled()) {
        followTargetValidForView = false;
        followTargetRaw = glm::vec3(0.0f);
        followTargetMotion = glm::vec3(0.0f);
        followTargetTracker.reset();
        return;
    }

    const int sceneIndex = followOrbit.sceneIndex();
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(models.size()) || !models[sceneIndex]) {
        followTargetValidForView = false;
        followTargetRaw = glm::vec3(0.0f);
        followTargetMotion = glm::vec3(0.0f);
        followTargetTracker.reset();
        return;
    }

    Model* targetModel = models[sceneIndex].get();
    if (!targetModel) {
        followTargetValidForView = false;
        followTargetRaw = glm::vec3(0.0f);
        followTargetMotion = glm::vec3(0.0f);
        followTargetTracker.reset();
        return;
    }

    const FollowSettings& settings = followOrbit.settings();
    const glm::vec3 targetCenterRaw = followAnchorPosition(*targetModel, settings);
    const FollowTargetFrame targetFrame = followTargetTracker.update(targetCenterRaw, deltaTime, settings.smoothSpeed);
    const glm::vec3 targetCenterForRig = targetFrame.motionCenter;
    followTargetRaw = targetFrame.rawCenter;
    followTargetMotion = targetFrame.motionCenter;
    // Use one tracked target for both orbit position and view lock to avoid
    // phase error oscillation when animation-driven bounds jitter.
    followTargetForView = targetCenterForRig;
    followTargetValidForView = true;
    const glm::vec3 modelForward = glm::vec3(targetModel->worldTransform[2]);
    FollowOrbitPose currentPose{};
    currentPose.position = cameraPos;
    currentPose.rotation = getEulerRotation();
    FollowOrbitPose nextPose{};
    if (mode == CameraMode::OrbitFollow)
    {
        nextPose = orbitRig.update(targetCenterForRig, deltaTime, currentPose);
    }
    else
    {
        nextPose = followOrbit.update(targetCenterForRig, modelForward, deltaTime, currentPose);
    }
    cameraPos = nextPose.position;
    // Hard constraint for follow/orbit cameras: always look at target center.
    const glm::vec3 toTarget = followTargetForView - cameraPos;
    if (glm::length(toTarget) > 0.001f)
    {
        const glm::vec3 front = glm::normalize(toTarget);
        const float yaw = std::atan2(-front.x, -front.z);
        const float pitch = std::asin(glm::clamp(front.y, -1.0f, 1.0f));
        setEulerRotation(glm::vec2(yaw, pitch));
    }
    else
    {
        setEulerRotation(nextPose.rotation);
    }

    glm::vec3 cameraFront = getForwardVector();
    const glm::vec3 toTargetNow = followTargetForView - cameraPos;
    const float targetDistance = glm::length(toTargetNow);
    const glm::vec3 toTargetDir = (targetDistance > 1e-6f) ? (toTargetNow / targetDistance) : glm::vec3(0.0f, 0.0f, -1.0f);
    const float frontDot = glm::dot(cameraFront, toTargetDir);
    const bool targetBehind = frontDot < 0.0f;
    const bool targetTooClose = targetDistance <= std::max(perspNear + 0.05f, followcam::kMinDistance * 0.35f);
    const bool badFraming = targetBehind || targetTooClose;
    const bool shouldLogFollowWarnings = (display && display->getActiveCamera() == this);
    followWarningCooldownSeconds = std::max(0.0f, followWarningCooldownSeconds - std::max(deltaTime, 0.0f));
    if (shouldLogFollowWarnings && badFraming && followWarningCooldownSeconds <= 0.0f)
    {
        std::cout << "[Camera][Follow][Warning] Target framing is invalid."
                  << " camera=\"" << cameraName << "\""
                  << " sceneIndex=" << sceneIndex
                  << " targetBehind=" << (targetBehind ? "true" : "false")
                  << " targetTooClose=" << (targetTooClose ? "true" : "false")
                  << " distance=" << targetDistance
                  << " near=" << perspNear
                  << " frontDot=" << frontDot
                  << " followDistance=" << settings.distance
                  << " yaw=" << settings.relativeYaw
                  << " pitch=" << settings.relativePitch
                  << std::endl;
        followWarningCooldownSeconds = 1.0f;
    }
    if (shouldLogFollowWarnings && !badFraming && followWarningActive)
    {
        std::cout << "[Camera][Follow] Target framing recovered."
                  << " camera=\"" << cameraName << "\""
                  << " sceneIndex=" << sceneIndex
                  << " distance=" << targetDistance
                  << " frontDot=" << frontDot
                  << std::endl;
    }
    followWarningActive = badFraming;

    // Keep view/projection state synchronized with follow updates so render/debug
    // use the exact same orbit pose in the current frame.
    updateCameraMatrices();
}
// New method implementations for camera mode management

void Camera::setMode(CameraMode newMode)
{
    if (mode == newMode)
    {
        return;
    }

    if (!canEnterMode(newMode))
    {
        std::cout << "[Camera][Warning] Rejected mode transition from "
                  << modeName(mode)
                  << " to "
                  << modeName(newMode)
                  << " (requires valid follow target)" << std::endl;
        return;
    }

    clearInputState();

    if (newMode == CameraMode::OrbitFollow && followOrbit.isEnabled())
    {
        // Keep orbit rig synchronized with current follow settings when entering orbit mode.
        orbitRig.configure(followOrbit.sceneIndex(), followOrbit.settings());
    }

    switch (newMode)
    {
        case CameraMode::FreeFly:
            controlsEnabled = true;
            break;
        case CameraMode::CharacterFollow:
        case CameraMode::OrbitFollow:
            controlsEnabled = false;
            break;
        case CameraMode::Fixed:
            controlsEnabled = false;
            break;
    }

    mode = newMode;
    std::cout << "[Camera] Mode changed to " << modeName(mode) << std::endl;
}

bool Camera::canEnterMode(CameraMode candidateMode) const
{
    return camera_mode_rules::canEnterMode(candidateMode, followOrbit.isEnabled(), followOrbit.sceneIndex());
}

const char* Camera::modeName(CameraMode value) const
{
    switch (value)
    {
        case CameraMode::FreeFly: return "FreeFly";
        case CameraMode::CharacterFollow: return "CharacterFollow";
        case CameraMode::OrbitFollow: return "OrbitFollow";
        case CameraMode::Fixed: return "Fixed";
        default: return "Unknown";
    }
}

Model* Camera::getFollowTarget(const std::vector<std::unique_ptr<Model>>& models) const
{
    const int sceneIndex = followOrbit.sceneIndex();
    if (sceneIndex >= 0 && sceneIndex < static_cast<int>(models.size())) {
        return models[sceneIndex].get();
    }
    return nullptr;
}

void Camera::setFreeFlyCamera(bool freeFly)
{
    if (freeFly) {
        setMode(CameraMode::FreeFly);
    } else {
        if (followOrbit.sceneIndex() >= 0 && followOrbit.isEnabled())
        {
            setMode(CameraMode::CharacterFollow);
        }
        else
        {
            setMode(CameraMode::Fixed);
        }
    }
}

bool Camera::isFreeFlyCamera() const
{
    return mode == CameraMode::FreeFly;
}

glm::vec2 Camera::getEulerRotation() const
{
    return cameraRotation;
}

void Camera::setEulerRotation(const glm::vec2& rotation)
{
    const glm::vec2 sanitized(
        rotation.x,
        std::clamp(rotation.y, -followcam::kFreeFlyMaxPitchRadians, followcam::kFreeFlyMaxPitchRadians));
    cameraOrientation = quaternionFromYawPitch(sanitized.x, sanitized.y);
    cameraRotation = sanitized;
    orientationSyncedEuler = sanitized;
}

glm::vec3 Camera::getForwardVector() const
{
    const glm::vec3 front = cameraOrientation * glm::vec3(0.0f, 0.0f, -1.0f);
    if (glm::length(front) <= 1e-6f)
    {
        return glm::vec3(0.0f, 0.0f, -1.0f);
    }
    return glm::normalize(front);
}

void Camera::syncOrientationFromEulerCacheIfNeeded()
{
    // Quaternion is authoritative; keep Euler cache in sync for UI/reporting only.
    syncEulerCacheFromOrientation();
}

void Camera::syncEulerCacheFromOrientation()
{
    cameraRotation = yawPitchFromQuaternion(cameraOrientation);
    orientationSyncedEuler = cameraRotation;
}

glm::quat Camera::quaternionFromYawPitch(float yaw, float pitch)
{
    const glm::quat yawQ = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::quat pitchQ = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));
    return glm::normalize(yawQ * pitchQ);
}

glm::vec2 Camera::yawPitchFromQuaternion(const glm::quat& orientation)
{
    const glm::vec3 front = glm::normalize(orientation * glm::vec3(0.0f, 0.0f, -1.0f));
    const float yaw = std::atan2(-front.x, -front.z);
    const float pitch = std::asin(glm::clamp(front.y, -1.0f, 1.0f));
    return glm::vec2(yaw, pitch);
}
