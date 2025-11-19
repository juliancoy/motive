#pragma once
#include <GLFW/glfw3.h>
#include <algorithm>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

// Forward declarations
class Engine;
class Camera;

class Display
{
public:
    Display(Engine* engine, int width = 800, int height = 600, const char* title = "Motive");
    ~Display();
    void createSwapchain();
    void createWindow(const char *title);
    void createSurface(GLFWwindow *window);    
    void addCamera(Camera* camera);

    void render();
    void createCommandPool();
    void createGraphicsPipeline();

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
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    bool firstFrame = true;
    std::string vertShaderPath;
    std::string fragShaderPath;

    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;

    int width;
    int height;

    // Camera instance
    std::vector<Camera*> cameras;

private:
    Engine* engine;
};
