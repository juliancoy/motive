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

class Display
{
public:
    Display(Engine* engine);
    ~Display();
    void updateCamera(uint32_t currentImage);
    void createSwapchain();
    void createWindow(int width, int height, const char *title);
    void createSurface(GLFWwindow *window);    

    void render();
    void createCommandPool();
    void createGraphicsPipeline();

    // Camera state
    glm::vec3 initialCameraPos = glm::vec3(0.0f, 0.0f, -3.0f);
    glm::vec2 initialCameraRotation = glm::vec2(glm::radians(-180.0f), 0.0f);
    glm::vec3 cameraPos = initialCameraPos;
    glm::vec2 cameraRotation = initialCameraRotation;
    float moveSpeed = 0.1f;

    // Input tracking
    bool rightMouseDown = false;
    glm::vec2 lastMousePos = glm::vec2(0.0f);
    bool keysPressed[5] = {false}; // W,A,S,D

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
    int graphicsQueueFamilyIndex;

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

    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    void* mappedUniformBuffer = nullptr;

private:
    Engine* engine;
};
