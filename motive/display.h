#pragma once
#include <GLFW/glfw3.h>
#include <algorithm>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/glm.hpp>
#include <chrono>
#include <vector>
#include <vulkan/vulkan.h>
#include "glyph.h"

// Forward declarations
class Engine;
class Camera;

class Display
{
public:
    Display(Engine* engine, int width = 800, int height = 600, const char* title = "Motive", bool disableCulling = false);
    ~Display();
    void createSwapchain();
    void createWindow(const char *title);
    void createSurface(GLFWwindow *window);    
    void addCamera(Camera* camera);
    void recreateSwapchain();

    void render();
    void createCommandPool();
    void createGraphicsPipeline();
    void handleFramebufferResize(int newWidth, int newHeight);
    void updateCameraViewports();

    // Input handling (forwarded to camera)
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPos(double xpos, double ypos);
    void handleKey(int key, int scancode, int action, int mods);

    GLFWwindow *window;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;


    VkCommandPool swapchainCmdPool;
    VkCommandBuffer swapchainCmdBuffer;
    VkCommandBuffer swapchainRecreationCmdBuffer;  // Separate command buffer for swapchain ops
    VkFence swapchainRecreationFence;             // Fence for synchronization

    VkCommandBuffer commandBuffer;
    VkPipelineLayout pipelineLayout;

    VkQueue graphicsQueue;
    VkCommandPool commandPool;
    VkImage depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView depthImageView;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory = VK_NULL_HANDLE;
    VkImageView colorImageView = VK_NULL_HANDLE;
    int graphicsQueueFamilyIndex;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    VkRenderPass renderPass;
    VkPipeline graphicsPipeline;
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;

    bool firstFrame = true;
    std::string vertShaderPath;
    std::string fragShaderPath;

    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;

    int width;
    int height;
    bool framebufferResized = false;
    float currentFps = 0.0f;
    bool cullingDisabled = false;

    // Camera instance
    std::vector<Camera*> cameras;
    float getCurrentFps() const { return currentFps; }

private:
    struct OverlayResources
    {
        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        void* mapped = nullptr;
        VkDeviceSize bufferSize = 0;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t offsetX = 0;
        uint32_t offsetY = 0;
    };

    Engine* engine;
    void cleanupSwapchainResources();
    void createOverlayBuffer(VkDeviceSize size);
    void destroyOverlayBuffer();
    void updateOverlayBitmap(float fps);
    void recordOverlayCopy(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    uint32_t fpsFrameCounter = 0;
    std::chrono::steady_clock::time_point fpsLastSampleTime = std::chrono::steady_clock::now();
    OverlayResources overlayResources;
};
