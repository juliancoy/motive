#pragma once

#include <vector>
#include <string>
#include <array>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>

class Model;  // Forward declaration

#include <stdexcept>
#include <cstring>
#include <iostream>
#include <string>
#include <set>
#include <fstream>
#include <vector>
#include <chrono>
#include "display.h"

struct UniformBufferObject {
    glm::mat4 model;
    glm::mat4 view;
    glm::mat4 proj;
};

std::vector<char> readSPIRVFile(const std::string &filename);

class Engine
{
public:
    Engine();
    ~Engine();

    std::vector<Model> models;

    VkInstance instance;
    VkDevice logicalDevice;
    VkPhysicalDevice physicalDevice;
    Display * display;

    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures features;
    VkPhysicalDeviceMemoryProperties memProperties;
    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool descriptorPool;
    VkDescriptorSet descriptorSet;

    void createInstance();
    void pickPhysicalDevice();
    void createLogicalDevice();
    void verifyDeviceSuitability();
    void renderLoop();
    void addModel(Model* model);
    void createWindow(int width, int height, const char* title);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);


    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, 
                    VkCommandPool commandPool, VkQueue graphicsQueue);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                    VkBuffer &buffer, VkDeviceMemory &bufferMemory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkShaderModule createShaderModule(const std::vector<char> &code);
    
    VkCommandPool commandPool;
    uint32_t graphicsQueueFamilyIndex;
    VkQueue graphicsQueue;
};
