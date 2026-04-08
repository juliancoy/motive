#pragma once

#include <vector>
#include <string>
#include <array>
#include <memory>
#include <mutex>
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include "object_transform.h"

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
#include "light_manager.h"
#include "buffer_manager.h"
#include "physics_interface.h"

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
    VkBuffer getLightBuffer() const { return lightManager.getBuffer(); }
    VkDeviceSize getLightUBOSize() const { return lightManager.getBufferSize(); }
    const Light& getLight() const { return lightManager.getLight(); }
    LightManager& getLightManager() { return lightManager; }
    VkSampleCountFlagBits getMsaaSampleCount() const { return msaaSampleCount; }

    void renderLoop();
    void addModel(std::unique_ptr<Model> model);
    void addCamera();
    Display* createWindow(int width, int height, const char* title, bool disableCulling = false, bool use2DPipeline = false, bool embeddedMode = false);
    VkCommandBuffer beginSingleTimeCommands();
    void endSingleTimeCommands(VkCommandBuffer commandBuffer);
    VkResult allocateDescriptorSet(VkDescriptorPool pool,
                                   VkDescriptorSetLayout layout,
                                   VkDescriptorSet& outSet);
    VkResult freeDescriptorSet(VkDescriptorPool pool, VkDescriptorSet descriptorSet);
    // Buffer management (delegated to BufferManager)
    void beginBatchUpload() { bufferManager.beginBatchUpload(); }
    void endBatchUpload() { bufferManager.endBatchUpload(); }
    void deferStagingBufferDestruction(VkBuffer buffer, VkDeviceMemory memory) { 
        bufferManager.deferStagingBufferDestruction(buffer, memory); 
    }
    BufferManager& getBufferManager() { return bufferManager; }
    void nameVulkanObject(uint64_t handle, VkObjectType type, const char* name);
    void createDescriptorSetLayouts();


    // Buffer operations (forward to BufferManager)
    void copyBuffer(VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size, 
                    VkCommandPool commandPool, VkQueue graphicsQueue) {
        bufferManager.copyBuffer(srcBuffer, dstBuffer, size, commandPool, graphicsQueue);
    }
    void createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                    VkBuffer &buffer, VkDeviceMemory &bufferMemory) {
        bufferManager.createBuffer(size, usage, properties, buffer, bufferMemory);
    }
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
        return bufferManager.findMemoryType(typeFilter, properties);
    }
    VkShaderModule createShaderModule(const std::vector<char> &code) {
        return bufferManager.createShaderModule(code);
    }
    void setLight(const Light& light) { lightManager.setLight(light); }
    void setMsaaSampleCount(VkSampleCountFlagBits requested);
    
    // Physics world access (new abstraction)
    motive::IPhysicsWorld* getPhysicsWorld() { return physicsWorld.get(); }
    const motive::IPhysicsWorld* getPhysicsWorld() const { return physicsWorld.get(); }
    void updatePhysics(float deltaTime);
    void syncPhysicsToModels();
    
    // Physics engine selection
    void setPhysicsEngine(motive::PhysicsEngineType type);
    motive::PhysicsEngineType getPhysicsEngineType() const { return physicsSettings.engineType; }
    motive::PhysicsSettings& getPhysicsSettings() { return physicsSettings; }
    const motive::PhysicsSettings& getPhysicsSettings() const { return physicsSettings; }
    
    // Parallel loading settings
    void setParallelModelLoading(bool enabled) { parallelModelLoading = enabled; }
    bool isParallelModelLoadingEnabled() const { return parallelModelLoading; }
    
    // Image to buffer copy for frame capture
    void copyImageToBuffer(VkImage srcImage, VkBuffer dstBuffer, 
                          uint32_t width, uint32_t height, 
                          VkFormat format, VkImageLayout srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);

    // Queue accessors for interop paths
    VkQueue getGraphicsQueue() const { return graphicsQueue; }
    uint32_t getGraphicsQueueFamilyIndex() const { return graphicsQueueFamilyIndex; }
    VkQueue getVideoQueue() const { return videoQueue; }
    uint32_t getVideoQueueFamilyIndex() const { return videoQueueFamilyIndex; }
    VkQueue getVideoDecodeQueue() const { return videoDecodeQueue; }
    uint32_t getVideoDecodeQueueFamilyIndex() const { return videoDecodeQueueFamilyIndex; }
    VkQueue getVideoEncodeQueue() const { return videoEncodeQueue; }
    uint32_t getVideoEncodeQueueFamilyIndex() const { return videoEncodeQueueFamilyIndex; }
    
    VkCommandPool &commandPool;
    uint32_t &graphicsQueueFamilyIndex;
    VkQueue &graphicsQueue;
    uint32_t &videoQueueFamilyIndex;
    VkQueue &videoQueue;
    uint32_t &videoDecodeQueueFamilyIndex;
    VkQueue &videoDecodeQueue;
    uint32_t &videoEncodeQueueFamilyIndex;
    VkQueue &videoEncodeQueue;
private:
    VkSampleCountFlagBits queryMaxUsableSampleCount() const;
    VkSampleCountFlagBits clampSampleCount(VkSampleCountFlagBits requested) const;


    VkSampleCountFlagBits msaaSampleCount = VK_SAMPLE_COUNT_1_BIT;

    std::mutex queueSubmitMutex;
    
    // Managers
    BufferManager bufferManager;
    LightManager lightManager;
    
    // Physics world (abstracted)
    std::unique_ptr<motive::IPhysicsWorld> physicsWorld;
    motive::PhysicsSettings physicsSettings;
    
    // Settings
    bool parallelModelLoading = false;  // Default to serial loading (safer)
};
