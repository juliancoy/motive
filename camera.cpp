#include "camera.h"
#include "engine.h"
#include "display.h"
#include "input_router.h"
#include <stdexcept>
#include <iostream>
#include <array>
#include <algorithm>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>

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
    cameraRotation = initialCameraRotation;

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
    updateCameraMatrices();
}

void Camera::reset()
{
    cameraPos = initialCameraPos;
    cameraRotation = initialCameraRotation;
    std::cout << "[Debug] Camera " << this << " reset. position (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z
              << ") rotation (" << cameraRotation.x << ", " << cameraRotation.y << ")" << std::endl;
}

void Camera::handleMouseButton(int button, int action, int mods)
{
    if (!controlsEnabled)
    {
        return;
    }
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        rightMouseDown = (action == GLFW_PRESS);
        if (rightMouseDown)
        {
            double x, y;
            if (windowHandle)
            {
                glfwGetCursorPos(windowHandle, &x, &y);
                lastMousePos = glm::vec2(x, y);
            }
            else
            {
                std::cerr << "[Warning] Camera " << this << " received mouse input without a valid window handle" << std::endl;
            }
        }
    }
}

void Camera::handleCursorPos(double xpos, double ypos)
{
    if (!controlsEnabled)
    {
        return;
    }

    if (rightMouseDown)
    {
        glm::vec2 currentPos(xpos, ypos);
        glm::vec2 delta = currentPos - lastMousePos;
        lastMousePos = currentPos;

        const float sensitivity = 0.005f;
        float deltaYaw = delta.x * sensitivity;
        float deltaPitch = -delta.y * sensitivity;
        
        // Clamp pitch
        deltaPitch = std::clamp(deltaPitch, -0.1f, 0.1f);
        
        if (characterTarget && followOrbit.isEnabled())
        {
            // In character follow mode: orbit camera around character
            // Update the follow offset angle
            addYawOffset(-deltaYaw);  // Invert for natural feel (drag left = look left)
            addPitchOffset(deltaPitch);
        }
        else
        {
            // Free-fly mode or no character: rotate camera directly
            cameraRotation.x += deltaYaw;
            cameraRotation.y -= delta.y * sensitivity;
            cameraRotation.y = std::clamp(cameraRotation.y, -1.4f, 1.4f);
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

    if (!controlsEnabled && freeFlyCamera)
    {
        return;
    }

    // Character locomotion only when free-fly is disabled.
    const bool characterDrivenMode = !freeFlyCamera;
    if (characterDrivenMode)
    {
        if (key == GLFW_KEY_W) keysPressed[0] = (action != GLFW_RELEASE);
        if (key == GLFW_KEY_A) keysPressed[1] = (action != GLFW_RELEASE);
        if (key == GLFW_KEY_S) keysPressed[2] = (action != GLFW_RELEASE);
        if (key == GLFW_KEY_D) keysPressed[3] = (action != GLFW_RELEASE);

        if (characterTarget && characterTarget->character.isControllable)
        {
            characterTarget->character.keyW = keysPressed[0];
            characterTarget->character.keyA = keysPressed[1];
            characterTarget->character.keyS = keysPressed[2];
            characterTarget->character.keyD = keysPressed[3];

            const float yaw = cameraRotation.x;
            const glm::vec3 forward(-sin(yaw), 0.0f, cos(yaw));
            const glm::vec3 right(cos(yaw), 0.0f, sin(yaw));

            glm::vec3 inputDir(0.0f);
            if (keysPressed[0]) inputDir += forward;
            if (keysPressed[1]) inputDir -= right;
            if (keysPressed[2]) inputDir -= forward;
            if (keysPressed[3]) inputDir += right;
            characterTarget->setCharacterInput(inputDir);

            if (key == GLFW_KEY_SPACE && action == GLFW_PRESS && characterTarget->character.isGrounded)
            {
                characterTarget->character.jumpRequested = true;
            }
        }

        if (key == GLFW_KEY_R && action == GLFW_PRESS)
        {
            reset();
        }
        return;
    }

    // Free-fly camera movement
    if (key == GLFW_KEY_W) keysPressed[0] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_A) keysPressed[1] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_S) keysPressed[2] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_D) keysPressed[3] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_Q) keysPressed[4] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_E) keysPressed[5] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        reset();
    }
}

void Camera::clearInputState()
{
    std::fill(std::begin(keysPressed), std::end(keysPressed), false);
    rightMouseDown = false;
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

    float yaw = cameraRotation.x;
    float pitch = cameraRotation.y;

    glm::vec3 front;
    front.x = cos(pitch) * sin(yaw);
    front.y = sin(pitch);
    front.z = -cos(pitch) * cos(yaw);
    front = glm::normalize(front);

    glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(front, worldUp));
    glm::vec3 up = glm::normalize(glm::cross(right, front));

    // Only process camera movement in free-fly mode, and only if character input is NOT active
    // When character mode is active, character moves and camera follows automatically
    bool shouldMoveCamera = false;
    if (controlsEnabled && freeFlyCamera)
    {
        if (display && display->getInputRouter())
        {
            shouldMoveCamera = !display->getInputRouter()->isCharacterInputActive();
        }
        else
        {
            shouldMoveCamera = true;
        }
    }

    if (shouldMoveCamera)
    {
        const bool* keys = keysPressed;
        glm::vec3 moveDir(0.0f);
        if (keys[0])
            moveDir += front;
        if (keys[1])
            moveDir -= right;
        if (keys[2])
            moveDir -= front;
        if (keys[3])
            moveDir += right;
        if (keys[4])
            moveDir -= up;
        if (keys[5])
            moveDir += up;

        if (glm::length(moveDir) > 0.0f)
        {
            cameraPos += glm::normalize(moveDir) * moveSpeed;
        }
    }

    viewMatrix = glm::lookAt(cameraPos, cameraPos + front, worldUp);
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


void Camera::setCharacterTarget(Model* model)
{
    characterTarget = model;
    if (model) {
        std::cout << "[Camera] Character target set: " << model << " controllable=" << model->character.isControllable << std::endl;
    } else {
        std::cout << "[Camera] Character target cleared" << std::endl;
    }
}

void Camera::setFollowTarget(int sceneIndex, const FollowSettings& settings)
{
    const FollowSettings sanitized = followcam::sanitizeSettings(settings);
    if (sceneIndex >= 0) {
        followOrbit.configure(sceneIndex, sanitized);
        freeFlyCamera = false;
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
}

void Camera::addPitchOffset(float deltaPitch)
{
    FollowSettings settings = followOrbit.settings();
    settings.relativePitch = std::clamp(settings.relativePitch + deltaPitch, -followcam::kMaxPitchRadians, followcam::kMaxPitchRadians);
    followOrbit.configure(followOrbit.sceneIndex(), settings);
}

void Camera::updateFollowOffsetFromCameraRotation()
{
    // When user orbits with right-drag, update the follow offset to match
    // This allows camera orbit while maintaining fixed distance from character
    FollowSettings settings = followOrbit.settings();
    
    // The camera's current rotation (cameraRotation) is absolute, we need relative
    // relativeYaw is offset FROM character's facing direction
    // For now, just store absolute and compute relative on each frame
    settings.relativeYaw = cameraRotation.x;
    settings.relativePitch = cameraRotation.y;
    
    followOrbit.configure(followOrbit.sceneIndex(), settings);
}

void Camera::updateFollow(float deltaTime, const std::vector<std::unique_ptr<Model>>& models)
{
    if (!followOrbit.isEnabled()) {
        return;
    }

    const int sceneIndex = followOrbit.sceneIndex();
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(models.size()) || !models[sceneIndex]) {
        return;
    }

    Model* targetModel = models[sceneIndex].get();
    if (!targetModel) {
        return;
    }

    const FollowSettings& settings = followOrbit.settings();
    const glm::vec3 targetCenter = followAnchorPosition(*targetModel, settings);

    constexpr float kPi = 3.14159265358979323846f;
    const float dist = std::max(followcam::kMinDistance, settings.distance);
    const float relativePitch = settings.relativePitch;
    const glm::vec3 modelForward = glm::vec3(targetModel->worldTransform[2]);
    const float targetYaw = FollowOrbit::computeTargetYaw(modelForward);
    // Keep semantics aligned with FollowOrbit::computePose:
    // relativeYaw=0 should position camera behind target.
    const float worldYaw = normalizeAngle(targetYaw + settings.relativeYaw + kPi);

    // Compute camera position from spherical coordinates around target
    // When relativeYaw=0, camera is behind character (+Z), when relativeYaw=pi, camera is in front
    // Standard spherical to cartesian conversion
    glm::vec3 cameraOffset;
    cameraOffset.x = sin(worldYaw) * cos(relativePitch) * dist;
    cameraOffset.y = sin(relativePitch) * dist;
    cameraOffset.z = cos(worldYaw) * cos(relativePitch) * dist;

    glm::vec3 desiredPos = targetCenter + cameraOffset;
    
    const float smoothSpeed = std::max(settings.smoothSpeed, 1.0f);
    float t = std::clamp(1.0f - std::exp(-smoothSpeed * std::max(deltaTime, 0.0f)), 0.0f, 1.0f);
    cameraPos = glm::mix(cameraPos, desiredPos, t);

    // Camera always looks at target
    glm::vec3 lookTarget = targetCenter + glm::vec3(0.0f, 0.5f, 0.0f);
    glm::vec3 toTarget = lookTarget - cameraPos;
    if (glm::length(toTarget) > 0.001f)
    {
        glm::vec3 front = glm::normalize(toTarget);
        float yaw = std::atan2(front.x, -front.z);
        float pitch = std::asin(glm::clamp(front.y, -1.0f, 1.0f));
        cameraRotation = glm::vec2(normalizeAngle(yaw), pitch);
    }

    glm::vec3 cameraFront;
    cameraFront.x = std::cos(cameraRotation.y) * std::sin(cameraRotation.x);
    cameraFront.y = std::sin(cameraRotation.y);
    cameraFront.z = -std::cos(cameraRotation.y) * std::cos(cameraRotation.x);
    if (glm::length(cameraFront) > 1e-6f)
    {
        cameraFront = glm::normalize(cameraFront);
    }
    const glm::vec3 toTargetNow = targetCenter - cameraPos;
    const float targetDistance = glm::length(toTargetNow);
    const glm::vec3 toTargetDir = (targetDistance > 1e-6f) ? (toTargetNow / targetDistance) : glm::vec3(0.0f, 0.0f, -1.0f);
    const float frontDot = glm::dot(cameraFront, toTargetDir);
    const bool targetBehind = frontDot < 0.0f;
    const bool targetTooClose = targetDistance <= std::max(perspNear + 0.05f, followcam::kMinDistance * 0.35f);
    const bool badFraming = targetBehind || targetTooClose;
    followWarningCooldownSeconds = std::max(0.0f, followWarningCooldownSeconds - std::max(deltaTime, 0.0f));
    if (badFraming && followWarningCooldownSeconds <= 0.0f)
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
    if (!badFraming && followWarningActive)
    {
        std::cout << "[Camera][Follow] Target framing recovered."
                  << " camera=\"" << cameraName << "\""
                  << " sceneIndex=" << sceneIndex
                  << " distance=" << targetDistance
                  << " frontDot=" << frontDot
                  << std::endl;
    }
    followWarningActive = badFraming;
}
