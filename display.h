#pragma once
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include <GLFW/glfw3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>
#include <vulkan/vulkan.h>

#include "text_rendering.h"
#include "swapchain_manager.h"
#include "frame_sync_state.h"

// Forward declarations
class Engine;
class Camera;
class InputRouter;
class Model;

class Display
{
public:
    struct WindowDebugState
    {
        bool focused = false;
        bool inputClearedOnFocusLoss = false;
        std::int64_t lastFocusChangeUnixMs = 0;
        std::int64_t lastKeyEventUnixMs = 0;
        int lastKey = GLFW_KEY_UNKNOWN;
        int lastScancode = 0;
        int lastAction = GLFW_RELEASE;
        int lastMods = 0;
    };

    struct ViewportSlot
    {
        Camera* camera = nullptr;
        float centerX = 0.0f;
        float centerY = 0.0f;
        float width = 1.0f;
        float height = 1.0f;
    };

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
    void updateRuntimeControllers(float deltaTime);
    void handleFramebufferResize(int newWidth, int newHeight);
    void updateCameraViewports();
    void setBackgroundColor(float r, float g, float b);
    void setViewportSlots(const std::vector<ViewportSlot>& viewportSlotsIn);
    void setMouseButtonEventCallback(std::function<void(int, int, int, double, double)> callback);
    void setCustomOverlayBitmap(const glyph::OverlayBitmap& bitmap, bool enabled);
    void clearCustomOverlayBitmap();
    void setEditorRenderModels(const std::vector<Model*>& models);
    void setDebugWindowTitle(const std::string& title);
    void shutdown();

    // Input handling (forwarded to camera)
    void handleMouseButton(int button, int action, int mods);
    void handleCursorPos(double xpos, double ypos);
    void handleKey(int key, int scancode, int action, int mods);
    void handleWindowFocusChanged(int focused);

    GLFWwindow* window = nullptr;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    
    // Accessors for swapchain resources (delegated to swapchainManager)
    VkSwapchainKHR getSwapchain() const { return swapchainManager.getSwapchain(); }
    VkRenderPass getRenderPass() const { return swapchainManager.getRenderPass(); }
    VkFramebuffer getFramebuffer(uint32_t index) const { return swapchainManager.getFramebuffer(index); }
    VkSemaphore getImageAvailableSemaphore(uint32_t frame) const { 
        return swapchainManager.getImageAvailableSemaphore(frame); 
    }
    VkSemaphore getRenderFinishedSemaphore(uint32_t frame) const { 
        return swapchainManager.getRenderFinishedSemaphore(frame); 
    }
    VkFence getInFlightFence(uint32_t frame) const { 
        return swapchainManager.getInFlightFence(frame); 
    }
    VkFormat getSwapchainImageFormat() const { return swapchainManager.getImageFormat(); }
    VkExtent2D getSwapchainExtent() const { return swapchainManager.getExtent(); }
    VkSampleCountFlagBits getMsaaSamples() const { return swapchainManager.getMsaaSamples(); }
    void setMsaaSamples(VkSampleCountFlagBits samples) { swapchainManager.setMsaaSamples(samples); }

    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;

    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    int graphicsQueueFamilyIndex = -1;

    // Pipeline arrays
    std::array<VkPipeline, 3> graphicsPipelines{};
    std::array<VkPipeline, 3> transparentGraphicsPipelines{};
    std::array<VkPipeline, 3> noDepthGraphicsPipelines{};
    std::array<VkPipeline, 3> noDepthTransparentGraphicsPipelines{};
    std::array<VkPipeline, 3> unlitGraphicsPipelines{};
    std::array<VkPipeline, 3> transparentUnlitGraphicsPipelines{};
    std::array<VkPipeline, 3> noDepthUnlitGraphicsPipelines{};
    std::array<VkPipeline, 3> noDepthTransparentUnlitGraphicsPipelines{};
    std::array<VkPipeline, 3> skinnedGraphicsPipelines{};
    std::array<VkPipeline, 3> transparentSkinnedGraphicsPipelines{};
    std::array<VkPipeline, 3> noDepthSkinnedGraphicsPipelines{};
    std::array<VkPipeline, 3> noDepthTransparentSkinnedGraphicsPipelines{};

    bool firstFrame = true;
    std::string vertShaderPath;
    std::string fragShaderPath;

    VkShaderModule vertShaderModule = VK_NULL_HANDLE;
    VkShaderModule skinnedVertShaderModule = VK_NULL_HANDLE;
    VkShaderModule fragShaderModule = VK_NULL_HANDLE;
    VkShaderModule unlitFragShaderModule = VK_NULL_HANDLE;

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
    void setActiveCamera(Camera* camera);
    Camera* getActiveCamera() const;
    uint32_t getLastRenderedImageIndex() const { return lastRenderedImageIndex; }

    // Access to swapchain manager
    SwapchainManager& getSwapchainManager() { return swapchainManager; }
    const SwapchainManager& getSwapchainManager() const { return swapchainManager; }

    // Input routing
    InputRouter* getInputRouter() { return inputRouter.get(); }
    const InputRouter* getInputRouter() const { return inputRouter.get(); }
    const WindowDebugState& getWindowDebugState() const { return windowDebugState; }
    const std::string& getWindowTitle() const { return windowTitle; }
    bool isNativeWindowFocused() const;

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
    FrameSyncState frameSyncState;

    SwapchainManager swapchainManager;

    void cleanupSwapchainResources();
    void createOverlayBuffer(VkDeviceSize size);
    void destroyOverlayBuffer();
    void updateOverlayBitmap(float fps);
    void recordOverlayCopy(VkCommandBuffer commandBuffer, uint32_t imageIndex);

    uint32_t fpsFrameCounter = 0;
    std::chrono::steady_clock::time_point fpsLastSampleTime = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point runtimeUpdateLastTime = std::chrono::steady_clock::now();
    bool runtimeUpdateInitialized = false;
    OverlayResources overlayResources;
    glyph::OverlayBitmap customOverlayBitmap;
    bool customOverlayEnabled = false;
    std::vector<Model*> editorRenderModels;
    std::vector<ViewportSlot> viewportSlots;
    bool useExplicitViewportSlots = false;
    std::function<void(int, int, int, double, double)> mouseButtonEventCallback;
    Camera* activeCamera = nullptr;
    std::unique_ptr<InputRouter> inputRouter;
    uint32_t lastRenderedImageIndex = 0;
    std::string windowTitle;
    WindowDebugState windowDebugState;
};
