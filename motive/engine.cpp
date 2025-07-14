#include "engine.h"
#include <stdexcept>
#include <cstring>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>
#include <iostream>
#include <string>
#include <set>
#include <fstream>
#include <vector>
#include <../tinygltf/tiny_gltf.h>
#include <chrono>

// Static mouse callback functions
static void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods)
{
    Engine *engine = static_cast<Engine *>(glfwGetWindowUserPointer(window));
    if (button == GLFW_MOUSE_BUTTON_RIGHT)
    {
        engine->rightMouseDown = (action == GLFW_PRESS);
        if (engine->rightMouseDown)
        {
            double x, y;
            glfwGetCursorPos(window, &x, &y);
            engine->lastMousePos = glm::vec2(x, y);
        }
    }
}

static void cursorPosCallback(GLFWwindow *window, double xpos, double ypos)
{
    Engine *engine = static_cast<Engine *>(glfwGetWindowUserPointer(window));
    if (engine->rightMouseDown)
    {
        glm::vec2 currentPos(xpos, ypos);
        glm::vec2 delta = currentPos - engine->lastMousePos;
        engine->lastMousePos = currentPos;

        // Adjust rotation based on mouse movement
        engine->cameraRotation.x += delta.x * 0.005f; //pitch
        engine->cameraRotation.y += delta.y * 0.005f; //yaw

    }
}

static void keyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    Engine *engine = static_cast<Engine *>(glfwGetWindowUserPointer(window));

    if (key == GLFW_KEY_W)
        engine->keysPressed[0] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_A)
        engine->keysPressed[1] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_S)
        engine->keysPressed[2] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_D)
        engine->keysPressed[3] = (action != GLFW_RELEASE);
    if (key == GLFW_KEY_R && action == GLFW_PRESS) {
        engine->cameraPos = engine->initialCameraPos;
        engine->cameraRotation = engine->initialCameraRotation;
        std::cout << "Camera reset to initial position\n";
    }
}

Engine::Engine()
{
    instance = VK_NULL_HANDLE;
    logicalDevice = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    graphicsQueue = VK_NULL_HANDLE;
    commandPool = VK_NULL_HANDLE;
    vertexBuffer = VK_NULL_HANDLE;
    vertexBufferMemory = VK_NULL_HANDLE;
    surface = VK_NULL_HANDLE;
    swapchain = VK_NULL_HANDLE;
    vertShaderModule = VK_NULL_HANDLE;
    fragShaderModule = VK_NULL_HANDLE;
    textureSampler = VK_NULL_HANDLE;
    gltfTextureImage = VK_NULL_HANDLE;
    gltfTextureImageMemory = VK_NULL_HANDLE;
    gltfTextureImageView = VK_NULL_HANDLE;

    if (!glfwInit())
    {
        throw std::runtime_error("Failed to initialize GLFW");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    createInstance();

    if (!instance)
    {
        throw std::runtime_error("Vulkan instance creation failed");
    }

    pickPhysicalDevice();

    if (!physicalDevice)
    {
        throw std::runtime_error("Failed to pick physical device");
    }

    createWindow(800, 600, "Engine"); // ❗️ Moved up before logical device

    createLogicalDevice(); // ❗️ Now we know the surface, so queue support can be verified

    createSwapchain();
    if (!swapchain)
    {
        throw std::runtime_error("Swapchain creation failed");
    }

    createCommandPool();
    if (!commandPool)
    {
        throw std::runtime_error("Command pool creation failed");
    }


    createGraphicsPipeline();
    if (!graphicsPipeline)
    {
        throw std::runtime_error("Graphics pipeline creation failed");
    }

    createTextureSampler();
    if (!textureSampler)
    {
        throw std::runtime_error("Texture sampler creation failed");
    }

    // Create uniform buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(UniformBufferObject);
    bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &uniformBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create uniform buffer!");
    }

    // Allocate uniform buffer memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(logicalDevice, uniformBuffer, &memRequirements);

    VkMemoryAllocateInfo memAllocInfo{};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    memAllocInfo.allocationSize = memRequirements.size;
    memAllocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(logicalDevice, &memAllocInfo, nullptr, &uniformBufferMemory) != VK_SUCCESS)
    if (vkAllocateMemory(logicalDevice, &memAllocInfo, nullptr, &uniformBufferMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate uniform buffer memory!");
    }

    vkBindBufferMemory(logicalDevice, uniformBuffer, uniformBufferMemory, 0);

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo descriptorAllocInfo{};
    descriptorAllocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    descriptorAllocInfo.descriptorPool = descriptorPool;
    descriptorAllocInfo.descriptorSetCount = 1;
    descriptorAllocInfo.pSetLayouts = &descriptorSetLayout;

    if (vkAllocateDescriptorSets(logicalDevice, &descriptorAllocInfo, &descriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate descriptor set!");
    }

    // Update descriptor set with UBO first (always valid)
    VkDescriptorBufferInfo bufferDescInfo{};
    bufferDescInfo.buffer = uniformBuffer;
    bufferDescInfo.offset = 0;
    bufferDescInfo.range = sizeof(UniformBufferObject);

    VkWriteDescriptorSet UBOdescriptorWrite{};
    UBOdescriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    UBOdescriptorWrite.dstSet = descriptorSet;
    UBOdescriptorWrite.dstBinding = 0;
    UBOdescriptorWrite.dstArrayElement = 0;
    UBOdescriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    UBOdescriptorWrite.descriptorCount = 1;
    UBOdescriptorWrite.pBufferInfo = &bufferDescInfo;

    // Only update UBO descriptor initially
    vkUpdateDescriptorSets(logicalDevice, 1, &UBOdescriptorWrite, 0, nullptr);

    // Ensure all operations are complete before proceeding
    vkDeviceWaitIdle(logicalDevice);

    // Create default white texture after descriptor set is allocated and updated
    createDefaultTexture();
}

void Engine::render()
{
    const int MAX_FRAMES_IN_FLIGHT = 2;
    if (imageAvailableSemaphores.empty())
    {
        imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
        inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);

        VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, {}, VK_FENCE_CREATE_SIGNALED_BIT};

        for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i)
        {
            if (vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &imageAvailableSemaphores[i]) != VK_SUCCESS ||
                vkCreateSemaphore(logicalDevice, &semaphoreInfo, nullptr, &renderFinishedSemaphores[i]) != VK_SUCCESS ||
                vkCreateFence(logicalDevice, &fenceInfo, nullptr, &inFlightFences[i]) != VK_SUCCESS)
            {
                throw std::runtime_error("Failed to create sync objects for frame " + std::to_string(i));
            }
        }
    }

    static size_t currentFrame = 0;

    if (firstFrame)
        firstFrame = false;
    else
    {
        vkWaitForFences(logicalDevice, 1, &inFlightFences[currentFrame], VK_TRUE, UINT64_MAX);
    }

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(logicalDevice, swapchain, UINT64_MAX,
                                            imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, &imageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        createSwapchain(); // optional
        return;
    }
    else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("Failed to acquire swapchain image.");
    }

    // Update uniform buffer with current transformation matrices
    updateUniformBuffer(imageIndex);

    vkResetFences(logicalDevice, 1, &inFlightFences[currentFrame]);

    // Ensure command buffer is not in use before resetting
    vkDeviceWaitIdle(logicalDevice);

    if (vkResetCommandBuffer(commandBuffer, 0) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to reset command buffer");
    }

    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to begin recording command buffer");
    }

    std::array<VkClearValue, 2> clearValues = {};
    clearValues[0].color = {{0.2f, 0.2f, 0.8f, 1.0f}}; // Color clear (blue)
    clearValues[1].depthStencil = {1.0f, 0}; // Depth clear

    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = renderPass;
    renderPassInfo.framebuffer = swapchainFramebuffers[imageIndex];
    renderPassInfo.renderArea = {{0, 0}, {800, 600}};
    renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
    renderPassInfo.pClearValues = clearValues.data();

    vkCmdBeginRenderPass(commandBuffer, &renderPassInfo, VK_SUBPASS_CONTENTS_INLINE);

    if (vertexBuffer != VK_NULL_HANDLE)
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, graphicsPipeline);

        // Bind descriptor set (contains both UBO and sampler)
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                pipelineLayout, 0, 1, &descriptorSet, 0, nullptr);

        VkBuffer vertexBuffers[] = {vertexBuffer};
        VkDeviceSize offsets[] = {0};
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);

        // Draw using the stored vertex count
        vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
    }

    vkCmdEndRenderPass(commandBuffer);

    if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to record command buffer");
    }

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkSemaphore waitSemaphores[] = {imageAvailableSemaphores[currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    VkSemaphore signalSemaphores[] = {renderFinishedSemaphores[currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, inFlightFences[currentFrame]) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to submit draw command buffer");
    }

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &swapchain;
    presentInfo.pImageIndices = &imageIndex;

    result = vkQueuePresentKHR(graphicsQueue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR)
    {
        createSwapchain(); // optional
        return;
    }
    else if (result != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to present swapchain image");
    }

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    glfwPollEvents();
}

void Engine::createInstance()
{
    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "Vulkan Renderer";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "No Engine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_0;

    const std::vector<const char *> validationLayers = {
        "VK_LAYER_KHRONOS_validation"};

    // Required extensions from GLFW
    uint32_t glfwExtensionCount = 0;
    const char **glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions || glfwExtensionCount == 0)
    {
        throw std::runtime_error("Failed to get required GLFW extensions");
    }

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;

    createInfo.enabledExtensionCount = glfwExtensionCount;
    createInfo.ppEnabledExtensionNames = glfwExtensions;

    createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, extensions.data());

    for (uint32_t i = 0; i < glfwExtensionCount; i++)
    {
        bool found = false;
        for (const auto &ext : extensions)
        {
            if (strcmp(ext.extensionName, glfwExtensions[i]) == 0)
            {
                found = true;
                break;
            }
        }
        if (!found)
        {
            throw std::runtime_error(std::string("Missing required extension: ") + glfwExtensions[i]);
        }
    }

    if (vkCreateInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create Vulkan instance");
    }
}

void Engine::pickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    if (deviceCount == 0)
    {
        throw std::runtime_error("Failed to find GPUs with Vulkan support");
    }

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());

    std::cout << "\nAvailable Physical Devices:\n";
    std::cout << "-------------------------\n";

    VkPhysicalDeviceProperties thisDeviceProps;
    for (const auto &device : devices)
    {
        // Device properties
        vkGetPhysicalDeviceProperties(device, &thisDeviceProps);
        std::cout << "Device: " << thisDeviceProps.deviceName << "\n";
    }

    physicalDevice = devices[0];

    std::cout << "\nSelected Device Properties:\n";
    std::cout << "-------------------------\n";
    vkGetPhysicalDeviceProperties(physicalDevice, &props);
    // for now default to the Vulkan default at #0

    // Device features
    vkGetPhysicalDeviceFeatures(physicalDevice, &features);

    // Memory properties
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    std::cout << "\nDevice: " << props.deviceName << "\n";
    std::cout << "  API Version: " << VK_VERSION_MAJOR(props.apiVersion) << "."
              << VK_VERSION_MINOR(props.apiVersion) << "."
              << VK_VERSION_PATCH(props.apiVersion) << "\n";
    std::cout << "  Driver Version: " << props.driverVersion << "\n";
    std::cout << "  Vendor ID: " << props.vendorID << "\n";
    std::cout << "  Device ID: " << props.deviceID << "\n";
    std::cout << "  Device Type: ";

    switch (props.deviceType)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
        std::cout << "Integrated GPU";
        break;
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
        std::cout << "Discrete GPU";
        break;
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
        std::cout << "Virtual GPU";
        break;
    case VK_PHYSICAL_DEVICE_TYPE_CPU:
        std::cout << "CPU";
        break;
    default:
        std::cout << "Other";
        break;
    }
    std::cout << "\n";

    // Print some important limits
    std::cout << "\n  Important Limits:\n";
    std::cout << "    maxImageDimension2D: " << props.limits.maxImageDimension2D << "\n";
    std::cout << "    maxPushConstantsSize: " << props.limits.maxPushConstantsSize << " bytes\n";
    std::cout << "    maxMemoryAllocationCount: " << props.limits.maxMemoryAllocationCount << "\n";
    std::cout << "    maxSamplerAllocationCount: " << props.limits.maxSamplerAllocationCount << "\n";
    std::cout << "    bufferImageGranularity: " << props.limits.bufferImageGranularity << " bytes\n";

    // Print some important features
    std::cout << "\n  Important Features:\n";
    std::cout << "    geometryShader: " << (features.geometryShader ? "Yes" : "No") << "\n";
    std::cout << "    tessellationShader: " << (features.tessellationShader ? "Yes" : "No") << "\n";
    std::cout << "    samplerAnisotropy: " << (features.samplerAnisotropy ? "Yes" : "No") << "\n";
    std::cout << "    textureCompressionBC: " << (features.textureCompressionBC ? "Yes" : "No") << "\n";

    // Print memory heaps
    std::cout << "\n  Memory Heaps (" << memProperties.memoryHeapCount << "):\n";
    for (uint32_t i = 0; i < memProperties.memoryHeapCount; ++i)
    {
        std::cout << "    Heap " << i << ": " << memProperties.memoryHeaps[i].size / (1024 * 1024) << " MB";
        if (memProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
        {
            std::cout << " (Device Local)";
        }
        std::cout << "\n";
    }

    if (!physicalDevice)
    {
        throw std::runtime_error("Physical device not initialized");
    }
}

void Engine::createLogicalDevice()
{
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    // Print enabled features
    VkPhysicalDeviceFeatures deviceFeatures{};
    deviceFeatures.samplerAnisotropy = VK_TRUE; // Enable commonly used features

    std::cout << "Creating Logical Device with Features:\n";
    std::cout << "  samplerAnisotropy: " << (deviceFeatures.samplerAnisotropy ? "Enabled" : "Disabled") << "\n";
    std::cout << "  geometryShader: " << (deviceFeatures.geometryShader ? "Enabled" : "Disabled") << "\n";
    std::cout << "  tessellationShader: " << (deviceFeatures.tessellationShader ? "Enabled" : "Disabled") << "\n";

    const std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    std::cout << "\nEnabled Device Extensions:\n";
    for (const auto &ext : deviceExtensions)
    {
        std::cout << "  " << ext << "\n";
    }

    VkDeviceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();

    // Enable validation layers if available
    const std::vector<const char *> validationLayers = {
        "VK_LAYER_KHRONOS_validation"};

    uint32_t layerCount;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> availableLayers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

    bool layersSupported = true;
    for (const char *layerName : validationLayers)
    {
        bool layerFound = false;
        for (const auto &layerProperties : availableLayers)
        {
            if (strcmp(layerName, layerProperties.layerName) == 0)
            {
                layerFound = true;
                break;
            }
        }
        if (!layerFound)
        {
            layersSupported = false;
            break;
        }
    }

    if (layersSupported)
    {
        createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
        createInfo.ppEnabledLayerNames = validationLayers.data();
        std::cout << "\nEnabled Validation Layers:\n";
        for (const auto &layer : validationLayers)
        {
            std::cout << "  " << layer << "\n";
        }
    }
    else
    {
        createInfo.enabledLayerCount = 0;
        std::cout << "\nValidation layers requested but not available!\n";
    }

    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &logicalDevice) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create logical device");
    }

    vkGetDeviceQueue(logicalDevice, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    std::cout << "\nLogical Device Created Successfully\n";
    std::cout << "Obtained Graphics Queue from Family: " << graphicsQueueFamilyIndex << "\n";

    // Create descriptor pool for UBO and sampler
    std::array<VkDescriptorPoolSize, 2> poolSizes{};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(logicalDevice, &poolInfo, nullptr, &descriptorPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor pool!");
    }
}

void Engine::createCommandPool()
{
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; // Add this

    if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &commandPool) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create command pool");
    }

    // Verify command pool is valid
    if (commandPool == VK_NULL_HANDLE) {
        throw std::runtime_error("Command pool is null after creation");
    }

    // Allocate command buffer
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(logicalDevice, &allocInfo, &commandBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate command buffers");
    }

    // Verify command buffer is valid
    if (commandBuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("Command buffer is null after allocation");
    }
}

void Engine::copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size)
{
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandPool = commandPool;
    allocInfo.commandBufferCount = 1;

    VkCommandBuffer commandBuffer;
    vkAllocateCommandBuffers(logicalDevice, &allocInfo, &commandBuffer);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = size;
    vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
}

void Engine::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                          VkBuffer &buffer, VkDeviceMemory &bufferMemory)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &buffer) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to create buffer!");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(logicalDevice, buffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &bufferMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("failed to allocate buffer memory!");
    }

    vkBindBufferMemory(logicalDevice, buffer, bufferMemory, 0);
}

void Engine::createVertexBuffer(const std::vector<Vertex> &vertices)
{
    vertexCount = static_cast<uint32_t>(vertices.size());
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    // Create staging buffer (CPU-visible)
    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    // Copy vertex data to staging buffer
    void *data;
    vkMapMemory(logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(logicalDevice, stagingBufferMemory);

    // Create device-local vertex buffer
    createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBuffer,
        vertexBufferMemory);

    // Copy data from staging buffer to device-local buffer
    copyBuffer(stagingBuffer, vertexBuffer, bufferSize);

    // Cleanup staging
    vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);
}

void Engine::createTextureImageView()
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create texture image view!");
    }
}


void Engine::updateUniformBuffer(uint32_t currentImage)
{
    static auto startTime = std::chrono::high_resolution_clock::now();
    auto currentTime = std::chrono::high_resolution_clock::now();
    float time = std::chrono::duration<float, std::chrono::seconds::period>(currentTime - startTime).count();

    UniformBufferObject ubo{};
    ubo.model = glm::mat4(1.0f);

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
    float moveSpeed = 0.003f * time;
    glm::vec3 moveDir(0.0f);

    // Flatten forward vector for movement (optional - remove to allow flying)
    //forward.y = 0;
    forward = glm::normalize(forward);

    if (keysPressed[0]) moveDir += forward;   // W
    if (keysPressed[1]) moveDir -= right;     // A
    if (keysPressed[2]) moveDir -= forward;   // S
    if (keysPressed[3]) moveDir += right;     // D

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
    ubo.view = rotation * translation;

    // === Projection Matrix (Vulkan fix) ===
    ubo.proj = glm::perspective(glm::radians(45.0f), 800.0f / 600.0f, 0.1f, 10.0f);
    ubo.proj[1][1] *= -1; // Vulkan Y-flip

    // === Upload to GPU ===
    void* data;
    vkMapMemory(logicalDevice, uniformBufferMemory, 0, sizeof(ubo), 0, &data);
    memcpy(data, &ubo, sizeof(ubo));
    vkUnmapMemory(logicalDevice, uniformBufferMemory);
}



void Engine::createDefaultTexture()
{
    // Create a 1x1 white texture
    const uint32_t width = 1;
    const uint32_t height = 1;
    const uint32_t pixel = 0xFFFFFFFF; // White RGBA

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(pixel);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create staging buffer for default texture!");
    }

    // Allocate staging buffer memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(logicalDevice, stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate staging buffer memory!");
    }

    vkBindBufferMemory(logicalDevice, stagingBuffer, stagingBufferMemory, 0);

    // Copy texture data to staging buffer
    void *data;
    vkMapMemory(logicalDevice, stagingBufferMemory, 0, sizeof(pixel), 0, &data);
    memcpy(data, &pixel, sizeof(pixel));
    vkUnmapMemory(logicalDevice, stagingBufferMemory);

    // Create texture image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(logicalDevice, &imageInfo, nullptr, &textureImage) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create default texture image!");
    }

    // Allocate texture image memory
    vkGetImageMemoryRequirements(logicalDevice, textureImage, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &textureImageMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate texture image memory!");
    }

    vkBindImageMemory(logicalDevice, textureImage, textureImageMemory, 0);

    // Transition image layout for copying
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(
        commandBuffer,
        stagingBuffer,
        textureImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    // Transition image layout to shader read
    VkImageMemoryBarrier shaderBarrier{};
    shaderBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    shaderBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    shaderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shaderBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    shaderBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    shaderBarrier.image = textureImage;
    shaderBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    shaderBarrier.subresourceRange.baseMipLevel = 0;
    shaderBarrier.subresourceRange.levelCount = 1;
    shaderBarrier.subresourceRange.baseArrayLayer = 0;
    shaderBarrier.subresourceRange.layerCount = 1;
    shaderBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    shaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &shaderBarrier);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    // Cleanup staging resources
    vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);

    // Create image view
    createTextureImageView();

    // Update descriptor set with sampler
    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = textureImageView;
    descImageInfo.sampler = textureSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = descriptorSet;
    descriptorWrite.dstBinding = 1;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;

    vkUpdateDescriptorSets(logicalDevice, 1, &descriptorWrite, 0, nullptr);
}

void Engine::createTextureSampler()
{
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = props.limits.maxSamplerAnisotropy;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(logicalDevice, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create texture sampler!");
    }
}

uint32_t Engine::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)))
        {
            if ((memProperties.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }
    }

    throw std::runtime_error("Failed to find suitable memory type");
}

Engine::~Engine()
{
    // Wait for device to be idle before cleanup
    if (logicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(logicalDevice);
    }

    // Cleanup swapchain resources
    if (swapchain != VK_NULL_HANDLE)
    {
        // Destroy framebuffers
        for (auto framebuffer : swapchainFramebuffers)
        {
            if (framebuffer != VK_NULL_HANDLE)
            {
                vkDestroyFramebuffer(logicalDevice, framebuffer, nullptr);
            }
        }

        // Destroy image views
        for (auto imageView : swapchainImageViews)
        {
            if (imageView != VK_NULL_HANDLE)
            {
                vkDestroyImageView(logicalDevice, imageView, nullptr);
            }
        }

        vkDestroySwapchainKHR(logicalDevice, swapchain, nullptr);
    }

    // Destroy render pass
    if (renderPass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(logicalDevice, renderPass, nullptr);
    }

    // Destroy surface
    if (surface != VK_NULL_HANDLE)
    {
        vkDestroySurfaceKHR(instance, surface, nullptr);
    }

    // Destroy pipeline
    if (graphicsPipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(logicalDevice, graphicsPipeline, nullptr);
    }

    // Destroy shader modules
    if (vertShaderModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(logicalDevice, vertShaderModule, nullptr);
    }
    if (fragShaderModule != VK_NULL_HANDLE)
    {
        vkDestroyShaderModule(logicalDevice, fragShaderModule, nullptr);
    }

    for (auto &sem : imageAvailableSemaphores)
        if (sem != VK_NULL_HANDLE)
            vkDestroySemaphore(logicalDevice, sem, nullptr);

    for (auto &sem : renderFinishedSemaphores)
        if (sem != VK_NULL_HANDLE)
            vkDestroySemaphore(logicalDevice, sem, nullptr);

    // Destroy uniform buffer
    if (uniformBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(logicalDevice, uniformBuffer, nullptr);
    }
    if (uniformBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(logicalDevice, uniformBufferMemory, nullptr);
    }

    // Destroy vertex buffer
    if (vertexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(logicalDevice, vertexBuffer, nullptr);
    }
    if (vertexBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(logicalDevice, vertexBufferMemory, nullptr);
    }

    // Destroy descriptor set layout
    if (descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(logicalDevice, descriptorSetLayout, nullptr);
    }

    // Destroy pipeline layout
    if (pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(logicalDevice, pipelineLayout, nullptr);
    }

    // Destroy descriptor pool
    if (descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(logicalDevice, descriptorPool, nullptr);
    }

    // Destroy texture resources
    if (textureImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(logicalDevice, textureImageView, nullptr);
    }
    if (textureImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(logicalDevice, textureImage, nullptr);
    }
    if (textureImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(logicalDevice, textureImageMemory, nullptr);
    }

    // Destroy depth resources
    if (depthImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(logicalDevice, depthImageView, nullptr);
    }
    if (depthImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(logicalDevice, depthImage, nullptr);
    }
    if (depthImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(logicalDevice, depthImageMemory, nullptr);
    }

    // Destroy GLTF texture resources
    if (gltfTextureImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(logicalDevice, gltfTextureImageView, nullptr);
    }
    if (gltfTextureImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(logicalDevice, gltfTextureImage, nullptr);
    }
    if (gltfTextureImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(logicalDevice, gltfTextureImageMemory, nullptr);
    }

    // Destroy sampler
    if (textureSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(logicalDevice, textureSampler, nullptr);
    }

    // Wait for all fences to complete before cleanup
    vkDeviceWaitIdle(logicalDevice);

    // Wait for all device operations to complete
    if (logicalDevice != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(logicalDevice);
    }

    // Free all command buffers first
    if (swapchainRecreationCmdBuffer != VK_NULL_HANDLE) {
        if (logicalDevice != VK_NULL_HANDLE && swapchainCmdPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(logicalDevice, swapchainCmdPool, 1, &swapchainRecreationCmdBuffer);
        }
        swapchainRecreationCmdBuffer = VK_NULL_HANDLE;
    }

    if (commandBuffer != VK_NULL_HANDLE) {
        if (logicalDevice != VK_NULL_HANDLE && commandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(logicalDevice, commandPool, 1, &commandBuffer);
        }
        commandBuffer = VK_NULL_HANDLE;
    }

    // Destroy command pools
    if (swapchainCmdPool != VK_NULL_HANDLE) {
        if (logicalDevice != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice, swapchainCmdPool, nullptr);
        }
        swapchainCmdPool = VK_NULL_HANDLE;
    }

    if (commandPool != VK_NULL_HANDLE) {
        if (logicalDevice != VK_NULL_HANDLE) {
            vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
        }
        commandPool = VK_NULL_HANDLE;
    }

    // Destroy fences after command buffers are freed
    for (auto &fence : inFlightFences) {
        if (fence != VK_NULL_HANDLE) {
            if (logicalDevice != VK_NULL_HANDLE) {
                vkDestroyFence(logicalDevice, fence, nullptr);
            }
            fence = VK_NULL_HANDLE;
        }
    }

    if (swapchainRecreationFence != VK_NULL_HANDLE) {
        if (logicalDevice != VK_NULL_HANDLE) {
            vkDestroyFence(logicalDevice, swapchainRecreationFence, nullptr);
        }
        swapchainRecreationFence = VK_NULL_HANDLE;
    }

    // Destroy command pool (this will free command buffers)
    if (commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(logicalDevice, commandPool, nullptr);
    }

    // Destroy device
    if (logicalDevice != VK_NULL_HANDLE)
    {
        vkDestroyDevice(logicalDevice, nullptr);
    }

    // Destroy instance
    if (instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(instance, nullptr);
    }

    // Destroy GLFW window
    if (window != nullptr)
    {
        glfwDestroyWindow(window);
    }

    // Terminate GLFW to clean up all resources
    glfwTerminate();
}

static std::vector<char> readSPIRVFile(const std::string &filename)
{
    std::ifstream file(filename, std::ios::ate | std::ios::binary);

    if (!file.is_open())
    {
        throw std::runtime_error("Failed to open SPIR-V file: " + filename);
    }

    size_t fileSize = (size_t)file.tellg();
    if (fileSize % 4 != 0)
    {
        throw std::runtime_error("SPIR-V file size not multiple of 4: " + filename);
    }

    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();

    // Verify SPIR-V magic number
    if (fileSize >= 4)
    {
        uint32_t magic = *reinterpret_cast<uint32_t *>(buffer.data());
        if (magic != 0x07230203)
        {
            throw std::runtime_error("Invalid SPIR-V magic number in file: " + filename);
        }
    }

    return buffer;
}

static VkShaderModule createShaderModule(VkDevice device, const std::vector<char> &code)
{
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());

    VkShaderModule shaderModule;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create shader module");
    }

    return shaderModule;
}

void Engine::createGraphicsPipeline()
{
    // Load compiled SPIR-V shaders
    std::string vertPath = "shaders/mainforward.vert.spv";
    std::string fragPath = "shaders/mainforward.frag.spv";

    auto vertShaderCode = readSPIRVFile(vertPath);
    auto fragShaderCode = readSPIRVFile(fragPath);

    // Create shader modules
    vertShaderModule = createShaderModule(logicalDevice, vertShaderCode);
    fragShaderModule = createShaderModule(logicalDevice, fragShaderCode);

    // Shader stages
    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};

    // Vertex input
    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

    auto bindingDescription = Vertex::getBindingDescription();
    auto attributeDescriptions = Vertex::getAttributeDescriptions();

    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(attributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = attributeDescriptions.data();

    // Input assembly
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // Viewport and scissor
    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 800.0f;
    viewport.height = 600.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {800, 600};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    // Rasterizer
    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    // rasterizer.cullMode = VK_CULL_MODE_NONE;
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    // Depth stencil state
    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};

    // Multisampling
    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    // Color blending
    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_FALSE;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    // Create descriptor set layout for UBO and sampler
    std::array<VkDescriptorSetLayoutBinding, 2> bindings{};

    // UBO at binding 0 (vertex stage)
    bindings[0].binding = 0;
    bindings[0].descriptorCount = 1;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    bindings[0].pImmutableSamplers = nullptr;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    // Sampler at binding 1 (fragment stage)
    bindings[1].binding = 1;
    bindings[1].descriptorCount = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].pImmutableSamplers = nullptr;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings = bindings.data();

    if (vkCreateDescriptorSetLayout(logicalDevice, &layoutInfo, nullptr, &descriptorSetLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create descriptor set layout!");
    }

    // Pipeline layout
    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout!");
    }

    // Graphics pipeline creation
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = nullptr;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    if (vkCreateGraphicsPipelines(logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipeline) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create graphics pipeline!");
    }
}

void Engine::createSurface(GLFWwindow *window)
{
    if (!instance)
    {
        throw std::runtime_error("Vulkan instance not initialized");
    }
    if (!window)
    {
        throw std::runtime_error("Invalid GLFW window");
    }

    VkResult result = glfwCreateWindowSurface(instance, window, nullptr, &surface);
    if (result != VK_SUCCESS || surface == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Failed to create window surface: " + std::to_string(result));
    }
}

void Engine::createSwapchain() {
    // Wait for any pending operations to complete
    vkDeviceWaitIdle(logicalDevice);
    int graphicsFamilyIndex = UINT32_MAX;
    // Find the graphics queue family index if not already set
    if (graphicsFamilyIndex == UINT32_MAX) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            VkBool32 presentSupport = false;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT && presentSupport) {
                graphicsFamilyIndex = i;
                break;
            }
        }

        if (graphicsFamilyIndex == UINT32_MAX) {
            throw std::runtime_error("Failed to find a suitable graphics queue family!");
        }
    }

    // Create the dedicated command pool for swapchain operations
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.queueFamilyIndex = graphicsFamilyIndex;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |  // Short-lived command buffers
                    VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;  // Allow individual resets
    
    if (vkCreateCommandPool(logicalDevice, &poolInfo, nullptr, &swapchainCmdPool) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain command pool!");
    }

    // Allocate the command buffers (both regular and recreation ones)
    VkCommandBufferAllocateInfo swapallocInfo{};
    swapallocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    swapallocInfo.commandPool = swapchainCmdPool;
    swapallocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    swapallocInfo.commandBufferCount = 1;

    if (vkAllocateCommandBuffers(logicalDevice, &swapallocInfo, &swapchainRecreationCmdBuffer) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate swapchain command buffer!");
    }

    // Create the fence for swapchain recreation synchronization
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;  // Start in signaled state
    
    if (vkCreateFence(logicalDevice, &fenceInfo, nullptr, &swapchainRecreationFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain recreation fence!");
    }

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities) != VK_SUCCESS) {
        throw std::runtime_error("Failed to get surface capabilities");
    }

    std::cout << "Surface Capabilities:\n";
    std::cout << "  minImageCount: " << capabilities.minImageCount << "\n";
    std::cout << "  maxImageCount: " << capabilities.maxImageCount << "\n";
    std::cout << "  currentExtent: " << capabilities.currentExtent.width << "x" << capabilities.currentExtent.height << "\n";

    // Query surface formats
    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());

    // Pick the first available format as default
    VkSurfaceFormatKHR surfaceFormat = formats[0];
    for (const auto& availableFormat : formats) {
        if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB && 
            availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            surfaceFormat = availableFormat;
            break;
        }
    }

    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == std::numeric_limits<uint32_t>::max()) {
        extent.width = std::clamp(extent.width, capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent.height = std::clamp(extent.height, capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    // Create swapchain
    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = surfaceFormat.format;
    swapchainInfo.imageColorSpace = surfaceFormat.colorSpace;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

    if (vkCreateSwapchainKHR(logicalDevice, &swapchainInfo, nullptr, &swapchain) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create swapchain!");
    }

    // Get swapchain images
    vkGetSwapchainImagesKHR(logicalDevice, swapchain, &imageCount, nullptr);
    std::vector<VkImage> swapchainImages(imageCount);
    vkGetSwapchainImagesKHR(logicalDevice, swapchain, &imageCount, swapchainImages.data());

    // Create image views
    swapchainImageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = surfaceFormat.format;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &swapchainImageViews[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create image views!");
        }
    }

    // Create render pass
    std::array<VkAttachmentDescription, 2> attachments = {};
    // Color attachment
    attachments[0].format = surfaceFormat.format;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    // Depth attachment
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference colorAttachmentRef = {};
    colorAttachmentRef.attachment = 0;
    colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentReference depthAttachmentRef = {};
    depthAttachmentRef.attachment = 1;
    depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass = {};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;
    subpass.pDepthStencilAttachment = &depthAttachmentRef;

    VkRenderPassCreateInfo renderPassInfo = {};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;

    if (vkCreateRenderPass(logicalDevice, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create render pass!");
    }

    // Create depth resources
    VkFormat depthFormat = VK_FORMAT_D32_SFLOAT;
    
    VkImageCreateInfo depthImageInfo{};
    depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
    depthImageInfo.extent = {extent.width, extent.height, 1};
    depthImageInfo.mipLevels = 1;
    depthImageInfo.arrayLayers = 1;
    depthImageInfo.format = depthFormat;
    depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateImage(logicalDevice, &depthImageInfo, nullptr, &depthImage) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image!");
    }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(logicalDevice, depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, 
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &depthImageMemory) != VK_SUCCESS) {
        throw std::runtime_error("Failed to allocate depth image memory!");
    }

    vkBindImageMemory(logicalDevice, depthImage, depthImageMemory, 0);

    VkImageViewCreateInfo depthViewInfo{};
    depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    depthViewInfo.image = depthImage;
    depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    depthViewInfo.format = depthFormat;
    depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    depthViewInfo.subresourceRange.baseMipLevel = 0;
    depthViewInfo.subresourceRange.levelCount = 1;
    depthViewInfo.subresourceRange.baseArrayLayer = 0;
    depthViewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(logicalDevice, &depthViewInfo, nullptr, &depthImageView) != VK_SUCCESS) {
        throw std::runtime_error("Failed to create depth image view!");
    }

    // Ensure swapchain recreation objects are valid
    if (swapchainRecreationFence == VK_NULL_HANDLE || swapchainRecreationCmdBuffer == VK_NULL_HANDLE) {
        throw std::runtime_error("Swapchain recreation objects not initialized");
    }

    // Wait for previous swapchain recreation to complete
    VkResult fenceWait = vkWaitForFences(logicalDevice, 1, &swapchainRecreationFence, VK_TRUE, UINT64_MAX);
    if (fenceWait != VK_SUCCESS) {
        throw std::runtime_error("Failed to wait for swapchain recreation fence");
    }
    if (vkResetFences(logicalDevice, 1, &swapchainRecreationFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to reset swapchain recreation fence");
    }

    // Reset and begin the command buffer
    if (vkResetCommandBuffer(swapchainRecreationCmdBuffer, 0) != VK_SUCCESS) {
        throw std::runtime_error("Failed to reset swapchain recreation command buffer");
    }
    
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    
    if (vkBeginCommandBuffer(swapchainRecreationCmdBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("Failed to begin command buffer for depth image layout transition");
    }

    // Transition depth image layout
    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = depthImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | 
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    vkCmdPipelineBarrier(
        swapchainRecreationCmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier
    );

    vkEndCommandBuffer(swapchainRecreationCmdBuffer);

    // Submit the command buffer
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &swapchainRecreationCmdBuffer;

    if (vkQueueSubmit(graphicsQueue, 1, &submitInfo, swapchainRecreationFence) != VK_SUCCESS) {
        throw std::runtime_error("Failed to submit depth image layout transition commands");
    }

    // Create framebuffers
    swapchainFramebuffers.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        std::array<VkImageView, 2> attachments = {
            swapchainImageViews[i],
            depthImageView
        };

        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        framebufferInfo.pAttachments = attachments.data();
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1;

        if (vkCreateFramebuffer(logicalDevice, &framebufferInfo, nullptr, &swapchainFramebuffers[i]) != VK_SUCCESS) {
            throw std::runtime_error("Failed to create framebuffer!");
        }
    }
}

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

// C interface for Python
Engine *engine;

extern "C"
{
    void load_geometry(Engine *engine, float *vertices, int count)
    {
        if (!engine || !vertices || count <= 0)
            return;

        const int floatsPerVertex = 8;
        if (count % floatsPerVertex != 0)
            return;

        int vertexCount = count / floatsPerVertex;
        std::vector<Vertex> verts(vertexCount);

        for (int i = 0; i < vertexCount; ++i)
        {
            verts[i].pos = {vertices[i * 8 + 0], vertices[i * 8 + 1], vertices[i * 8 + 2]};
            verts[i].normal = {vertices[i * 8 + 3], vertices[i * 8 + 4], vertices[i * 8 + 5]};
            verts[i].texCoord = {vertices[i * 8 + 6], vertices[i * 8 + 7]};
        }

        engine->createVertexBuffer(verts);
    }

    void *create_engine()
    {
        engine = new Engine();
        return engine;
    }

    void destroy_engine(void *engine)
    {
        delete static_cast<Engine *>(engine);
    }

    void create_engine_window(void *engine, int width, int height, const char *title)
    {
        static_cast<Engine *>(engine)->createWindow(width, height, title);
    }

    void render(Engine *engine)
    {
        if (!engine)
            return;

        // Render frame
        engine->render();
    }
}

void Engine::createWindow(int width, int height, const char *title)
{
    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        throw std::runtime_error("Failed to create GLFW window");
    }

    // Set window user pointer and callbacks
    glfwSetWindowUserPointer(window, this);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetKeyCallback(window, keyCallback);
    std::cout << "Registered key callback for WASD movement\n";

    createSurface(window);
    if (!surface)
    {
        throw std::runtime_error("Surface creation failed");
    }

    // --------- GET THE QUEUE FAMILY INDEX ----------
    // Queue families
    // Check queue family support
    uint32_t queueFamilyCount = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &queueFamilyCount, queueFamilies.data());

    bool foundSuitableQueue = false;
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);

        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
        {
            graphicsQueueFamilyIndex = i;
            foundSuitableQueue = true;
            break;
        }
    }

    if (!foundSuitableQueue)
    {
        throw std::runtime_error("Failed to find queue family with graphics and present support");
    }

    // Verify swapchain support
    uint32_t extensionCount;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    for (const auto &extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    if (!requiredExtensions.empty())
    {
        throw std::runtime_error("Missing required device extensions");
    }
}

void Engine::loadFromFile(const std::string &gltfPath)
{
    tinygltf::Model model;
    tinygltf::TinyGLTF loader;
    std::string err;
    std::string warn;

    bool ret = false;
    size_t extPos = gltfPath.find_last_of('.');
    if (extPos != std::string::npos)
    {
        std::string ext = gltfPath.substr(extPos);
        if (ext == ".glb")
        {
            ret = loader.LoadBinaryFromFile(&model, &err, &warn, gltfPath);
        }
        else if (ext == ".gltf")
        {
            ret = loader.LoadASCIIFromFile(&model, &err, &warn, gltfPath);
        }
        else
        {
            throw std::runtime_error("Unsupported file extension: " + ext);
        }
        std::cout << "Loading GLTF file: " << gltfPath << "\n";
        std::cout << "  Vertices: " << model.meshes[0].primitives[0].attributes.size() << " attributes\n";
    }
    else
    {
        throw std::runtime_error("File has no extension: " + gltfPath);
    }
    if (!warn.empty())
    {
        std::cout << "GLTF warning: " << warn << std::endl;
    }
    if (!err.empty())
    {
        throw std::runtime_error("GLTF error: " + err);
    }
    if (!ret)
    {
        throw std::runtime_error("Failed to load GLTF file: " + gltfPath);
    }

    if (model.meshes.empty())
    {
        throw std::runtime_error("GLTF file contains no meshes: " + gltfPath);
    }

    std::cout << "Primitive count " << model.meshes.size() << " meshes\n";

    std::vector<Vertex> vertices;
    size_t totalVertexCount = 0;

    for (size_t meshIdx = 0; meshIdx < model.meshes.size(); ++meshIdx)
    {
        const auto &mesh = model.meshes[meshIdx];
        size_t meshVertexCount = 0;

        for (const auto &primitive : mesh.primitives)
        {
            if (primitive.attributes.find("POSITION") == primitive.attributes.end() ||
                primitive.attributes.find("NORMAL") == primitive.attributes.end() ||
                primitive.attributes.find("TEXCOORD_0") == primitive.attributes.end())
            {
                std::cerr << "Skipping primitive in mesh " << meshIdx << " due to missing attributes.\n";
                continue;
            }

            const auto &posAccessor = model.accessors[primitive.attributes.at("POSITION")];
            const auto &posView = model.bufferViews[posAccessor.bufferView];
            const auto &posBuffer = model.buffers[posView.buffer];
            const float *positions = reinterpret_cast<const float *>(
                &posBuffer.data[posView.byteOffset + posAccessor.byteOffset]);

            const auto &normalAccessor = model.accessors[primitive.attributes.at("NORMAL")];
            const auto &normalView = model.bufferViews[normalAccessor.bufferView];
            const auto &normalBuffer = model.buffers[normalView.buffer];
            const float *normals = reinterpret_cast<const float *>(
                &normalBuffer.data[normalView.byteOffset + normalAccessor.byteOffset]);

            const auto &texAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];
            const auto &texView = model.bufferViews[texAccessor.bufferView];
            const auto &texBuffer = model.buffers[texView.buffer];
            const float *texCoords = reinterpret_cast<const float *>(
                &texBuffer.data[texView.byteOffset + texAccessor.byteOffset]);

            if (posAccessor.count != normalAccessor.count || posAccessor.count != texAccessor.count)
            {
                throw std::runtime_error("GLTF attribute counts don't match in mesh " + std::to_string(meshIdx));
            }

            std::vector<Vertex> localVertices(posAccessor.count);

            for (size_t i = 0; i < posAccessor.count; ++i)
            {
                Vertex vertex{};
                vertex.pos[0] = positions[i * 3];
                vertex.pos[1] = positions[i * 3 + 1];
                vertex.pos[2] = positions[i * 3 + 2];

                vertex.normal[0] = normals[i * 3];
                vertex.normal[1] = normals[i * 3 + 1];
                vertex.normal[2] = normals[i * 3 + 2];

                vertex.texCoord[0] = texCoords[i * 2];
                vertex.texCoord[1] = texCoords[i * 2 + 1];

                localVertices[i] = vertex;
            }

            if (primitive.indices < 0)
            {
                throw std::runtime_error("GLTF primitive has no indices");
            }

            const auto &indexAccessor = model.accessors[primitive.indices];
            const auto &indexView = model.bufferViews[indexAccessor.bufferView];
            const auto &indexBuffer = model.buffers[indexView.buffer];
            const void *indexData = &indexBuffer.data[indexView.byteOffset + indexAccessor.byteOffset];
            std::vector<uint32_t> indices;
            for (size_t i = 0; i < indexAccessor.count; ++i)
            {
                switch (indexAccessor.componentType)
                {
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                    indices.push_back(((const uint16_t *)indexData)[i]);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                    indices.push_back(((const uint32_t *)indexData)[i]);
                    break;
                case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                    indices.push_back(((const uint8_t *)indexData)[i]);
                    break;
                default:
                    throw std::runtime_error("Unsupported index component type");
                }
            }

            // Use indices to construct vertex list properly
            for (uint32_t idx : indices)
            {
                vertices.push_back(localVertices[idx]);
            }

            meshVertexCount += indices.size(); // Count actual triangles submitted
        }

        std::cout << "Mesh " << meshIdx << " vertex count: " << meshVertexCount << "\n";
        totalVertexCount += meshVertexCount;
    }
    // center and normalize
    glm::vec3 minPos(FLT_MAX);
    glm::vec3 maxPos(-FLT_MAX);

    // Step 1: Compute bounding box
    for (const auto &v : vertices)
    {
        glm::vec3 pos(v.pos[0], v.pos[1], v.pos[2]);
        minPos = glm::min(minPos, pos);
        maxPos = glm::max(maxPos, pos);
    }

    // Step 2: Compute center and scale factor
    glm::vec3 center = (minPos + maxPos) * 0.5f;
    glm::vec3 size = maxPos - minPos;
    float maxExtent = glm::compMax(size); // Largest axis

    // Step 3: Normalize vertices in-place
    for (auto &v : vertices)
    {
        v.pos[0] = (v.pos[0] - center.x) / maxExtent;
        v.pos[1] = (v.pos[1] - center.y) / maxExtent;
        v.pos[2] = (v.pos[2] - center.z) / maxExtent;
    }

    // Rotate 90° around Z axis (existing)
    glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 0, 1));

    // Rotate 90° around Y axis (new)
    glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(0, 1, 0));

    // Rotate 90° around X axis (new)
    glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), glm::radians(90.0f), glm::vec3(-1, 0, 0));
    glm::mat4 combinedRotation = rotX;// identity glm::mat4(1.0f);

    for (auto &v : vertices)
    {
        glm::vec4 pos(v.pos[0], v.pos[1], v.pos[2], 1.0f);
        glm::vec4 rotated = combinedRotation * pos;
        v.pos[0] = rotated.x;
        v.pos[1] = rotated.y;
        v.pos[2] = rotated.z;

        // Optional: rotate normals too (use w = 0 for direction vectors)
        glm::vec4 normal(v.normal[0], v.normal[1], v.normal[2], 0.0f);
        normal = combinedRotation * normal;
        v.normal[0] = normal.x;
        v.normal[1] = normal.y;
        v.normal[2] = normal.z;
    }

    /*
    std::ofstream outFile("vertices.txt");
    if (!outFile.is_open())
    {
        throw std::runtime_error("Failed to open vertices.txt for writing");
    }

    for (const auto &vertex : vertices)
    {
        outFile << vertex.pos[0] << ' ' << vertex.pos[1] << ' ' << vertex.pos[2] << ' '
                << vertex.normal[0] << ' ' << vertex.normal[1] << ' ' << vertex.normal[2] << ' '
                << vertex.texCoord[0] << ' ' << vertex.texCoord[1] << '\n';
    }

    outFile.close();
    std::cout << "Saved " << vertices.size() << " vertices to vertices.txt\n";
    */

    // Create vertex buffer with proper Vertex struct
    VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    // Create staging buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = bufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create staging buffer!");
    }

    // Allocate staging buffer memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(logicalDevice, stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate staging buffer memory!");
    }

    vkBindBufferMemory(logicalDevice, stagingBuffer, stagingBufferMemory, 0);

    // Copy vertex data
    void *data;
    vkMapMemory(logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), bufferSize);
    vkUnmapMemory(logicalDevice, stagingBufferMemory);

    // Create device local vertex buffer
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &vertexBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create vertex buffer!");
    }

    // Allocate vertex buffer memory
    vkGetBufferMemoryRequirements(logicalDevice, vertexBuffer, &memRequirements);
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                               VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &vertexBufferMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate vertex buffer memory!");
    }

    vkBindBufferMemory(logicalDevice, vertexBuffer, vertexBufferMemory, 0);

    // Copy from staging to device local buffer
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

    vkBeginCommandBuffer(commandBuffer, &beginInfo);

    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(commandBuffer, stagingBuffer, vertexBuffer, 1, &copyRegion);

    vkEndCommandBuffer(commandBuffer);

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
    vkQueueWaitIdle(graphicsQueue);

    // Cleanup staging resources
    vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);

    vertexCount = vertices.size();
    std::cout << "Loaded " << vertexCount << " vertices\n";

    // Load texture if available
    if (!model.textures.empty())
    {
        const auto &texture = model.textures[0];
        const auto &image = model.images[texture.source];

        // Create texture image
        VkBuffer stagingBuffer;
        VkDeviceMemory stagingBufferMemory;

        VkDeviceSize imageSize = image.width * image.height * 4;

        // Create staging buffer
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = imageSize;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        if (vkCreateBuffer(logicalDevice, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create staging buffer for texture!");
        }

        // Allocate staging buffer memory
        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(logicalDevice, stagingBuffer, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

        if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate staging buffer memory!");
        }

        vkBindBufferMemory(logicalDevice, stagingBuffer, stagingBufferMemory, 0);

        // Copy texture data to staging buffer
        void *data;
        vkMapMemory(logicalDevice, stagingBufferMemory, 0, imageSize, 0, &data);
        memcpy(data, image.image.data(), imageSize);
        vkUnmapMemory(logicalDevice, stagingBufferMemory);

        // Create texture image
        imgCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imgCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imgCreateInfo.extent.width = image.width;
        imgCreateInfo.extent.height = image.height;
        imgCreateInfo.extent.depth = 1;
        imgCreateInfo.mipLevels = 1;
        imgCreateInfo.arrayLayers = 1;
        imgCreateInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        imgCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imgCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imgCreateInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imgCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        imgCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(logicalDevice, &imgCreateInfo, nullptr, &gltfTextureImage) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create texture image!");
        }

        // Allocate texture image memory
        vkGetImageMemoryRequirements(logicalDevice, gltfTextureImage, &memRequirements);

        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits,
                                                   VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(logicalDevice, &allocInfo, nullptr, &gltfTextureImageMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate texture image memory!");
        }

        vkBindImageMemory(logicalDevice, gltfTextureImage, gltfTextureImageMemory, 0);

        // Transition image layout for copying
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        vkBeginCommandBuffer(commandBuffer, &beginInfo);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = gltfTextureImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.baseMipLevel = 0;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &barrier);

        // Copy buffer to image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {
            static_cast<uint32_t>(image.width),
            static_cast<uint32_t>(image.height),
            1u};

        vkCmdCopyBufferToImage(
            commandBuffer,
            stagingBuffer,
            gltfTextureImage,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            1,
            &region);

        // Transition image layout to shader read
        VkImageMemoryBarrier shaderBarrier{};
        shaderBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        shaderBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        shaderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        shaderBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shaderBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        shaderBarrier.image = gltfTextureImage;
        shaderBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        shaderBarrier.subresourceRange.baseMipLevel = 0;
        shaderBarrier.subresourceRange.levelCount = 1;
        shaderBarrier.subresourceRange.baseArrayLayer = 0;
        shaderBarrier.subresourceRange.layerCount = 1;
        shaderBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        shaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0,
            0, nullptr,
            0, nullptr,
            1, &shaderBarrier);

        vkEndCommandBuffer(commandBuffer);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;

        vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(graphicsQueue);

        // Create GLTF texture image view
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = gltfTextureImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0u;
        viewInfo.subresourceRange.levelCount = 1u;
        viewInfo.subresourceRange.baseArrayLayer = 0u;
        viewInfo.subresourceRange.layerCount = 1u;

        if (vkCreateImageView(logicalDevice, &viewInfo, nullptr, &gltfTextureImageView) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create GLTF texture image view!");
        }

        // Update descriptor set with both UBO and sampler
        VkDescriptorBufferInfo bufferDescInfo{};
        bufferDescInfo.buffer = uniformBuffer;
        bufferDescInfo.offset = 0;
        bufferDescInfo.range = sizeof(UniformBufferObject);

        VkDescriptorImageInfo imageInfo{};
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = gltfTextureImageView;
        imageInfo.sampler = textureSampler;

        std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

        // UBO at binding 0
        descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[0].dstSet = descriptorSet;
        descriptorWrites[0].dstBinding = 0;
        descriptorWrites[0].dstArrayElement = 0;
        descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        descriptorWrites[0].descriptorCount = 1;
        descriptorWrites[0].pBufferInfo = &bufferDescInfo;

        // Sampler at binding 1
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;

        vkUpdateDescriptorSets(logicalDevice, static_cast<uint32_t>(descriptorWrites.size()), descriptorWrites.data(), 0, nullptr);

        // Cleanup staging resources
        vkDestroyBuffer(logicalDevice, stagingBuffer, nullptr);
        vkFreeMemory(logicalDevice, stagingBufferMemory, nullptr);
    }
}
