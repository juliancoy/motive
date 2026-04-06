#pragma once

#include <vulkan/vulkan.h>
#include "light.h"

class LightManager
{
public:
    LightManager();
    ~LightManager();

    // Non-copyable
    LightManager(const LightManager&) = delete;
    LightManager& operator=(const LightManager&) = delete;
    // Movable
    LightManager(LightManager&& other) noexcept;
    LightManager& operator=(LightManager&& other) noexcept;

    // Initialize with Vulkan device and buffer creation function
    void initialize(VkDevice device, 
                    uint32_t graphicsQueueFamilyIndex,
                    VkPhysicalDeviceMemoryProperties memProperties);
    void shutdown();
    bool isInitialized() const { return device_ != VK_NULL_HANDLE; }

    // Light management
    void setLight(const Light& light);
    const Light& getLight() const { return currentLight_; }

    // Buffer access for rendering
    VkBuffer getBuffer() const { return lightUBO_; }
    VkDeviceSize getBufferSize() const { return sizeof(LightUBOData); }

private:
    void createBuffer(VkDeviceSize size, 
                      VkBufferUsageFlags usage, 
                      VkMemoryPropertyFlags properties);
    void destroyBuffer();
    void updateBuffer();
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    VkDevice device_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex_ = 0;
    VkPhysicalDeviceMemoryProperties memProperties_{};

    Light currentLight_;
    VkBuffer lightUBO_ = VK_NULL_HANDLE;
    VkDeviceMemory lightUBOMemory_ = VK_NULL_HANDLE;
    void* lightUBOMapped_ = nullptr;
};
