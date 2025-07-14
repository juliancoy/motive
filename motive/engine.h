#pragma once

#include <vector>
#include <string>
#include <array>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>


struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription() {
        VkVertexInputBindingDescription bindingDescription{};
        bindingDescription.binding = 0;
        bindingDescription.stride = sizeof(Vertex);
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        return bindingDescription;
    }

    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions() {
        std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

        // Position
        attributeDescriptions[0].binding = 0;
        attributeDescriptions[0].location = 0;
        attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[0].offset = offsetof(Vertex, pos);

        // Normal
        attributeDescriptions[1].binding = 0;
        attributeDescriptions[1].location = 1;
        attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
        attributeDescriptions[1].offset = offsetof(Vertex, normal);

        // Texture Coordinate
        attributeDescriptions[2].binding = 0;
        attributeDescriptions[2].location = 2;
        attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
        attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

        return attributeDescriptions;
    }
};

class Engine
{
public:
    Engine();
    ~Engine();

    void createWindow(int width, int height, const char *title);
    void cleanup();
    void loadFromFile(const std::string &gltfPath);
    void render();
    void createVertexBuffer(const std::vector<Vertex>& vertices);

    GLFWwindow *window;

private:
    VkInstance instance;
    VkDevice logicalDevice;
    VkPhysicalDevice physicalDevice;
    VkQueue graphicsQueue;
    VkCommandPool commandPool;
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    VkSurfaceKHR surface;
    VkSwapchainKHR swapchain;
    VkCommandBuffer commandBuffer;
    VkRenderPass renderPass;
    VkPipeline graphicsPipeline;
    std::vector<VkImageView> swapchainImageViews;
    std::vector<VkFramebuffer> swapchainFramebuffers;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;

    int graphicsQueueFamilyIndex;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties memProperties;
    VkShaderModule vertShaderModule;
    VkShaderModule fragShaderModule;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;
    VkPipelineLayout pipelineLayout;
    VkSampler textureSampler;
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    VkImage gltfTextureImage;
    VkDeviceMemory gltfTextureImageMemory;
    VkImageView gltfTextureImageView;
    VkImageCreateInfo imgCreateInfo{};
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    uint32_t vertexCount;
    bool firstFrame = true;
    std::string vertShaderPath;
    std::string fragShaderPath;

    void createInstance();
    void createSurface(GLFWwindow *window);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createCommandPool();
    void createSwapchain();
    void createGraphicsPipeline();
    void createTextureSampler();
    void createTextureImageView();
    void createDefaultTexture();
    void verifyDeviceSuitability();
    void updateUniformBuffer(uint32_t currentImage);
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                          VkBuffer& buffer, VkDeviceMemory& bufferMemory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
};
