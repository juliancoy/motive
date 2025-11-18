#include "camera.h"
#include "engine.h"
#include "display.h"
#include <stdexcept>
#include <iostream>

namespace {
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

Camera::Camera(Engine* engine,
               Display* display,
               const glm::vec3& initialPos,
               const glm::vec2& initialRot)
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
    if (engine && engine->logicalDevice != VK_NULL_HANDLE) {
        if (cameraTransformUBO != VK_NULL_HANDLE) {
            vkDestroyBuffer(engine->logicalDevice, cameraTransformUBO, nullptr);
            cameraTransformUBO = VK_NULL_HANDLE;
        }
        if (cameraTransformDeviceUBO != VK_NULL_HANDLE) {
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

    // Update descriptor set with UBO
    VkDescriptorBufferInfo bufferDescInfo{};
    bufferDescInfo.buffer = cameraTransformUBO;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = sizeof(CameraTransform);

    VkWriteDescriptorSet UBOdescriptorWrite{};
    UBOdescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    UBOdescriptorWrite.dstSet = descriptorSet;
    UBOdescriptorWrite.dstBinding = 0;
    UBOdescriptorWrite.dstArrayElement = 0;
    UBOdescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    UBOdescriptorWrite.descriptorCount = 1;
    UBOdescriptorWrite.pBufferInfo = &bufferDescInfo;

    vkUpdateDescriptorSets(engine->logicalDevice, 1, &UBOdescriptorWrite, 0, nullptr);
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
    if (rightMouseDown)
    {
        glm::vec2 currentPos(xpos, ypos);
        glm::vec2 delta = currentPos - lastMousePos;
        lastMousePos = currentPos;

        // Adjust rotation based on mouse movement
        cameraRotation.x += delta.x * 0.005f; //pitch
        cameraRotation.y += delta.y * 0.005f; //yaw
    }
}

void Camera::handleKey(int key, int scancode, int action, int mods)
{
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
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        reset();
    }
}

void Camera::setWindow(GLFWwindow* window)
{
    windowHandle = window;
    registerWindowCallbacks();
}

void Camera::updateCameraMatrices()
{
    CameraTransform camera0TransformUBO{};

    // === Camera Rotation Angles ===
    float yaw = cameraRotation.x;    // Y-axis rotation (left/right)
    float pitch = cameraRotation.y;  // X-axis rotation (up/down)

    // === Forward vector from pitch & yaw ===
    glm::vec3 front;
    front.x = cos(pitch) * sin(yaw);      // note: swapped from your original
    front.y = sin(pitch);
    front.z = -cos(pitch) * cos(yaw);
    front = glm::normalize(front);

    // === Calculate up & right from front ===
    glm::vec3 worldUp = glm::vec3(0, 1, 0);
    // Apply full camera rotation to movement vectors
    glm::vec3 forward = glm::vec3(cos(pitch) * sin(yaw), sin(pitch), -cos(pitch) * cos(yaw));
    glm::vec3 right = glm::normalize(glm::cross(glm::vec3(0, 1, 0), forward));
    
    glm::vec3 up = glm::normalize(glm::cross(front, right));

    // === Movement Handling (flattened) ===
    glm::vec3 moveDir(0.0f);

    // Flatten forward vector for movement (optional - remove to allow flying)
    //forward.y = 0;
    forward = glm::normalize(forward);

    if (keysPressed[0]) moveDir += forward;   // W
    if (keysPressed[1]) moveDir -= right;     // A
    if (keysPressed[2]) moveDir -= forward;   // S
    if (keysPressed[3]) moveDir += right;     // D
    if (keysPressed[4]) moveDir -= worldUp;   // Q (down)
    if (keysPressed[5]) moveDir += worldUp;   // E (up)

    if (glm::length(moveDir) > 0.0f) {
        cameraPos += glm::normalize(moveDir) * moveSpeed;
        std::cout << "Moving to: (" << cameraPos.x << ", " << cameraPos.y << ", " << cameraPos.z << ")\n";
    }

    // === Construct View Matrix ===
    // Basis matrix from right, up, front
    glm::mat4 rotation = glm::mat4(1.0f);
    rotation[0] = glm::vec4(right, 0.0f);
    rotation[1] = glm::vec4(up, 0.0f);
    rotation[2] = glm::vec4(-front, 0.0f);  // invert forward

    // Translation matrix
    glm::mat4 translation = glm::translate(glm::mat4(1.0f), -cameraPos);

    // Combine rotation and translation
    camera0TransformUBO.view = rotation * translation;

    // === Projection Matrix (Vulkan fix) ===
    camera0TransformUBO.proj = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 10.0f);
    camera0TransformUBO.proj[1][1] *= -1; // Vulkan Y-flip

    // === Upload to GPU ===
    if (camera0TransformMappedUBO) {
        memcpy(camera0TransformMappedUBO, &camera0TransformUBO, sizeof(camera0TransformUBO));
    } else {
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
