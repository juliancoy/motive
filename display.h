#pragma once
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>
#include <vulkan/vulkan.h>

#include "glyph.h"

// Forward declarations
class Engine;
class Camera;

class Display
{
public:
    Display(Engine* engine,
            int width = 800,
            int height = 600,
            const char* title = "Motive",
            bool disableCulling = false,
            bool use2DPipeline = false,
            bool embeddedMode = false);
    ~Display();

    void createSwapchain();
    void createWindow(const char* title);
    void createSurface(GLFWwindow* window);
    void addCamera(Camera* camera);
    void recreateSwapchain();

    void render();
    void createCommandPool();
    void createGraphicsPipeline();
    void handleFramebufferResize(int newWidth, int newHeight);
    void updateCameraViewports();
    void setBackgroundColor(float r, float g, float b);
    void shutdown();

    // Input handling (forwarded to camera)
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPos(double xpos, double ypos);
    void handleKey(int key, int scancode, int action, int mods);
    void handleWindowFocusChanged(int focused);

    GLFWwindow* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;

    VkCommandPool swapchainCmdPool = VK_NULL_HANDLE;
    VkCommandBuffer swapchainCmdBuffer = VK_NULL_HANDLE;
    VkCommandBuffer swapchainRecreationCmdBuffer = VK_NULL_HANDLE;
    VkFence swapchainRecreationFence = VK_NULL_HANDLE;

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkImage depthImage = VK_NULL_HANDLE;
    VkDeviceMemory depthImageMemory = VK_NULL_HANDLE;
    VkImageView depthImageView = VK_NULL_HANDLE;
    VkImage colorImage = VK_NULL_HANDLE;
    VkDeviceMemory colorImageMemory = VK_NULL_HANDLE;
    VkImageView colorImageView = VK_NULL_HANDLE;
    int graphicsQueueFamilyIndex = -1;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::array<VkPipeline, 3> graphicsPipelines{};
    std::array<VkPipeline, 3> transparentGraphicsPipelines{};
    std::array<VkPipeline, 3> skinnedGraphicsPipelines{};
    std::array<VkPipeline, 3> transparentSkinnedGraphicsPipelines{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    std::vector<VkFence> imagesInFlight;

    bool firstFrame = true;
    size_t currentFrame = 0;
    std::string vertShaderPath;
    std::string fragShaderPath;

    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule skinnedVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;

    int width;
    int height;
    bool framebufferResized = false;
    float currentFps = 0.0f;
    bool cullingDisabled = false;
    bool use2DPipeline = false;
    bool embeddedMode = false;
    float bgColorR = 0.2f;
    float bgColorG = 0.2f;
    float bgColorB = 0.8f;

    // Camera instances (multiple cameras supported)
    std::vector<Camera*> cameras;
    float getCurrentFps() const { return currentFps; }

    // Camera management
    Camera* findCameraByName(const std::string& name) const;
    Camera* createCamera(const std::string& name = "",
                         const glm::vec3& initialPos = glm::vec3(0.0f, 0.0f, -3.0f),
                         const glm::vec2& initialRot = glm::vec2(glm::radians(-180.0f), 0.0f));
    void removeCamera(Camera* camera);
    Camera* getActiveCamera() const { return cameras.empty() ? nullptr : cameras[0]; }

private:
    std::vector<std::unique_ptr<Camera>> ownedCameras;

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

    Engine* engine = nullptr;
    std::mutex renderMutex;
    bool shuttingDown = false;

    void cleanupSwapchainResources();
    void createOverlayBuffer(VkDeviceSize size);
    void destroyOverlayBuffer();
    void updateOverlayBitmap(float fps);
    void recordOverlayCopy(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    uint32_t fpsFrameCounter = 0;
    std::chrono::steady_clock::time_point fpsLastSampleTime = std::chrono::steady_clock::now();
    OverlayResources overlayResources;
};
