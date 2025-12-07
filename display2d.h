#pragma once
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <vector>
#include <chrono>
#include "video.h"

class Engine;
class Primitive;

class Display2D
{
public:
    Display2D(Engine* engine, int width = 800, int height = 600, const char* title = "Motive 2D");
    ~Display2D();

    void renderFrame(Primitive* videoPrimitive,
                     Primitive* overlayPrimitive,
                     const video::VideoColorInfo& colorInfo,
                     uint32_t videoWidth,
                     uint32_t videoHeight,
                     VkExtent2D overlaySize,
                     VkOffset2D overlayOffset);
    bool shouldClose() const;
    void pollEvents() const;

    GLFWwindow* window = nullptr;
    int width = 0;
    int height = 0;

private:
    Engine* engine = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_B8G8R8A8_SRGB;
    VkExtent2D swapchainExtent{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;

    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline computePipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    std::vector<VkDescriptorSet> descriptorSets;

    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    size_t currentFrame = 0;

    void createWindow(const char* title);
    void createSurface();
    void createSwapchain();
    void cleanupSwapchain();
    void createCommandResources();
    void createComputeResources();
    void recreateSwapchain();
};
