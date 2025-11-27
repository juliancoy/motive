#include "camera.h"
#include "engine.h"
#include "display.h"
#include <stdexcept>
#include <iostream>
#include <array>
#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>

namespace
{
    void ForwardMouseButton(GLFWwindow *window, int button, int action, int mods)
    {
        Display *display = static_cast<Display *>(glfwGetWindowUserPointer(window));
        if (!display)
            return;
        for (auto *camera : display->cameras)
        {
            if (camera)
            {
                camera->handleMouseButton(button, action, mods);
            }
        }
    }

    void ForwardCursorPos(GLFWwindow *window, double xpos, double ypos)
    {
        Display *display = static_cast<Display *>(glfwGetWindowUserPointer(window));
        if (!display)
            return;
        for (auto *camera : display->cameras)
        {
            if (camera)
            {
                camera->handleCursorPos(xpos, ypos);
            }
        }
    }

    void ForwardKey(GLFWwindow *window, int key, int scancode, int action, int mods)
    {
        Display *display = static_cast<Display *>(glfwGetWindowUserPointer(window));
        if (!display)
            return;
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
        {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
            return;
        }
        for (auto *camera : display->cameras)
        {
            if (camera)
            {
                camera->handleKey(key, scancode, action, mods);
            }
        }
    }
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

    if (vkAllocateDescriptorSets(engine->logicalDevice, &descriptorAllocInfo, &descriptorSet) != VK_SUCCESS)
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
        cameraRotation.x += delta.x * sensitivity; // yaw (left/right)
        cameraRotation.y -= delta.y * sensitivity; // pitch (up/down)
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
    if (key == GLFW_KEY_W)
        keysPressed[0] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_A)
        keysPressed[1] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_S)
        keysPressed[2] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_D)
        keysPressed[3] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_Q)
        keysPressed[4] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_E)
        keysPressed[5] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_R && action == GLFW_PRESS)
    {
        reset();
    }
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

    if (controlsEnabled)
    {
        glm::vec3 moveDir(0.0f);
        if (keysPressed[0])
            moveDir += front;
        if (keysPressed[1])
            moveDir -= right;
        if (keysPressed[2])
            moveDir -= front;
        if (keysPressed[3])
            moveDir += right;
        if (keysPressed[4])
            moveDir -= up;
        if (keysPressed[5])
            moveDir += up;

        if (glm::length(moveDir) > 0.0f)
        {
            cameraPos += glm::normalize(moveDir) * moveSpeed;
            std::cout << "[Camera] Moving to: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")\n";
        }
    }

    camera0TransformUBO.view = glm::lookAt(cameraPos, cameraPos + front, worldUp);

    float aspect = (height > 0.0f) ? (width / height) : (800.0f / 600.0f);

    if (useOrthoProjection)
    {
        const float halfWidth = orthoWidth * 0.5f;
        const float halfHeight = orthoHeight * 0.5f;
        camera0TransformUBO.proj = glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, orthoNear, orthoFar);
    }
    else
    {
        camera0TransformUBO.proj = glm::perspectiveRH_ZO(glm::radians(45.0f), aspect, 0.1f, 100.0f);
    }
    camera0TransformUBO.proj[1][1] *= -1;

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
    if (!display)
    {
        return;
    }
    if (!windowHandle)
    {
        windowHandle = display->window;
    }
    if (!windowHandle)
    {
        return;
    }

    glfwSetWindowUserPointer(windowHandle, display);
    glfwSetMouseButtonCallback(windowHandle, ForwardMouseButton);
    glfwSetCursorPosCallback(windowHandle, ForwardCursorPos);
    glfwSetKeyCallback(windowHandle, ForwardKey);
}

glm::mat4 Camera::getViewMatrix() const
{
    // This would return the current view matrix
    // For now, we'll return identity - this can be implemented based on the updateCameraMatrices logic
    return glm::mat4(1.0f);
}

glm::mat4 Camera::getProjectionMatrix() const
{
    // This would return the current projection matrix
    // For now, we'll return identity - this can be implemented based on the updateCameraMatrices logic
    return glm::mat4(1.0f);
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

void Camera::setControlsEnabled(bool enabled)
{
    controlsEnabled = enabled;
    if (!controlsEnabled)
    {
        std::fill(std::begin(keysPressed), std::end(keysPressed), false);
        rightMouseDown = false;
    }
}
