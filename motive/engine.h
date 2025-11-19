#pragma once

#include <vector>
#include <string>
#include <array>
#include <memory>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

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
#include "graphicsdevice.h"
#include "camera.h"
#include "light.h"

struct ObjectTransform {
    glm::mat4 model;
};

class Engine
{
public:
    Engine();
    ~Engine();

    RenderDevice renderDevice;
    VkInstance &instance;
    VkDevice &logicalDevice;
    VkPhysicalDevice &physicalDevice;
    std::vector<std::unique_ptr<Model>> models;
    std::vector<Display *> displays;

    VkPhysicalDeviceProperties &props;
    VkPhysicalDeviceFeatures &features;
    VkPhysicalDeviceMemoryProperties &memProperties;
    VkDescriptorSetLayout &descriptorSetLayout;
    VkDescriptorSetLayout &primitiveDescriptorSetLayout;
    VkDescriptorPool &descriptorPool;
    VkDescriptorSet descriptorSet;
    VkBuffer getLightBuffer() const { return lightUBO; }
    VkDeviceSize getLightUBOSize() const { return sizeof(LightUBOData); }
    const Light& getLight() const { return currentLight; }
    VkSampleCountFlagBits getMsaaSampleCount() const { return msaaSampleCount; }

    void renderLoop();
    void addModel(std::unique_ptr<Model> model);
    void addCamera();
    Display* createWindow(int width, int height, const char* title);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    void nameVulkanObject(uint64_t handle, VkObjectType type, const char* name);
    void createDescriptorSetLayouts();


    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, 
                    VkCommandPool commandPool, VkQueue graphicsQueue);
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                    VkBuffer &buffer, VkDeviceMemory &bufferMemory);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkShaderModule createShaderModule(const std::vector<char> &code);
    void setLight(const Light& light);
    void setMsaaSampleCount(VkSampleCountFlagBits requested);
    
    VkCommandPool &commandPool;
    uint32_t &graphicsQueueFamilyIndex;
    VkQueue &graphicsQueue;
private:
    void createLightResources();
    void destroyLightResources();
    void updateLightBuffer();
    VkSampleCountFlagBits queryMaxUsableSampleCount() const;
    VkSampleCountFlagBits clampSampleCount(VkSampleCountFlagBits requested) const;

    Light currentLight;
    VkBuffer lightUBO = VK_NULL_HANDLE;
    VkDeviceMemory lightUBOMemory = VK_NULL_HANDLE;
    void* lightUBOMapped = nullptr;
    VkSampleCountFlagBits msaaSampleCount = VK_SAMPLE_COUNT_1_BIT;
};
