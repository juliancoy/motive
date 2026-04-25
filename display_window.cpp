#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "input_router.h"

void Display::createWindow(const char* title)
{
    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    if (embeddedMode)
    {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }

    window = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window)
    {
        throw std::runtime_error("Failed to create GLFW window");
    }

    glfwSetWindowUserPointer(window, this);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* win, int fbWidth, int fbHeight) {
        auto* display = static_cast<Display*>(glfwGetWindowUserPointer(win));
        if (display)
        {
            display->handleFramebufferResize(fbWidth, fbHeight);
        }
    });
    glfwSetMouseButtonCallback(window, [](GLFWwindow* win, int button, int action, int mods) {
        auto* display = static_cast<Display*>(glfwGetWindowUserPointer(win));
        if (display)
        {
            display->handleMouseButton(button, action, mods);
        }
    });
    glfwSetCursorPosCallback(window, [](GLFWwindow* win, double xpos, double ypos) {
        auto* display = static_cast<Display*>(glfwGetWindowUserPointer(win));
        if (display)
        {
            display->handleCursorPos(xpos, ypos);
        }
    });
    glfwSetKeyCallback(window, [](GLFWwindow* win, int key, int scancode, int action, int mods) {
        auto* display = static_cast<Display*>(glfwGetWindowUserPointer(win));
        if (display)
        {
            display->handleKey(key, scancode, action, mods);
        }
    });
    glfwSetWindowFocusCallback(window, [](GLFWwindow* win, int focused) {
        auto* display = static_cast<Display*>(glfwGetWindowUserPointer(win));
        if (display)
        {
            display->handleWindowFocusChanged(focused);
        }
    });

    createSurface(window);
    if (surface == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Surface creation failed");
    }

    uint32_t queueFamilyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, nullptr);
    std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(engine->physicalDevice, &queueFamilyCount, queueFamilies.data());

    bool foundSuitableQueue = false;
    for (uint32_t i = 0; i < queueFamilyCount; ++i)
    {
        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(engine->physicalDevice, i, surface, &presentSupport);

        if ((queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && presentSupport)
        {
            graphicsQueueFamilyIndex = static_cast<int>(i);
            foundSuitableQueue = true;
            break;
        }
    }

    if (!foundSuitableQueue)
    {
        throw std::runtime_error("Failed to find queue family with graphics and present support");
    }

    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(engine->physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> availableExtensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(engine->physicalDevice, nullptr, &extensionCount, availableExtensions.data());

    std::set<std::string> requiredExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    for (const auto& extension : availableExtensions)
    {
        requiredExtensions.erase(extension.extensionName);
    }

    if (!requiredExtensions.empty())
    {
        throw std::runtime_error("Missing required device extensions");
    }
}

void Display::createSurface(GLFWwindow* window)
{
    if (!engine->instance)
    {
        throw std::runtime_error("Vulkan engine->instance not initialized");
    }
    if (!window)
    {
        throw std::runtime_error("Invalid GLFW window");
    }

    VkResult result = glfwCreateWindowSurface(engine->instance, window, nullptr, &surface);
    if (result != VK_SUCCESS || surface == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Failed to create window surface: " + std::to_string(result));
    }
}

void Display::handleFramebufferResize(int newWidth, int newHeight)
{
    framebufferResized = true;
    width = std::max(0, newWidth);
    height = std::max(0, newHeight);
}

void Display::handleMouseButton(int button, int action, int mods)
{
    double xpos = 0.0;
    double ypos = 0.0;
    if (window)
    {
        glfwGetCursorPos(window, &xpos, &ypos);
    }

    if (mouseButtonEventCallback)
    {
        mouseButtonEventCallback(button, action, mods, xpos, ypos);
    }

    Camera* camera = getActiveCamera();
    if (camera)
    {
        camera->handleMouseButton(button, action, mods);
    }
}

void Display::setMouseButtonEventCallback(std::function<void(int, int, int, double, double)> callback)
{
    mouseButtonEventCallback = std::move(callback);
}

void Display::handleCursorPos(double xpos, double ypos)
{
    Camera* camera = getActiveCamera();
    if (camera)
    {
        camera->handleCursorPos(xpos, ypos);
    }
}

void Display::handleKey(int key, int scancode, int action, int mods)
{
    if (window && key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
    {
        glfwSetWindowShouldClose(window, GLFW_TRUE);
        return;
    }

    Camera* camera = getActiveCamera();
    if (!camera)
    {
        return;
    }

    if (inputRouter)
    {
        // InputRouter handles character input in CharacterFollow mode
        // In FreeFly mode, camera handles its own movement
        inputRouter->handleKey(key, action);
    }

    camera->handleKey(key, scancode, action, mods);
}

void Display::handleWindowFocusChanged(int focused)
{
    if (focused)
    {
        return;
    }

    if (inputRouter)
    {
        inputRouter->clearInput();
    }

    Camera* camera = getActiveCamera();
    if (camera)
    {
        camera->clearInputState();
    }
}
