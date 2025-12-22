#pragma once

#include <vulkan/vulkan.h>
#include "engine.h"

// Minimal adapter that mirrors the sample VulkanDeviceContext getters
// but reuses the Engine-owned instance/device/queues instead of creating new ones.
// Extend as needed to satisfy decoder/encoder usage.
class EngineDeviceContextAdapter
{
public:
    explicit EngineDeviceContextAdapter(Engine& engineRef);

    VkInstance getInstance() const { return instance; }
    VkPhysicalDevice getPhysicalDevice() const { return physicalDevice; }
    VkDevice getDevice() const { return device; }

    int32_t GetVideoDecodeQueueFamilyIdx() const { return static_cast<int32_t>(videoDecodeQueueFamilyIndex); }
    int32_t GetVideoEncodeQueueFamilyIdx() const { return static_cast<int32_t>(videoEncodeQueueFamilyIndex); }
    int32_t GetGfxQueueFamilyIdx() const { return static_cast<int32_t>(graphicsQueueFamilyIndex); }

    VkQueue GetVideoDecodeQueue(int32_t index = 0) const { (void)index; return videoDecodeQueue; }
    VkQueue GetVideoEncodeQueue(int32_t index = 0) const { (void)index; return videoEncodeQueue; }
    VkQueue GetGfxQueue() const { return graphicsQueue; }

    VkQueueFlags GetVideoDecodeQueueFlag() const { return VK_QUEUE_VIDEO_DECODE_BIT_KHR; }
    VkQueueFlags GetVideoEncodeQueueFlag() const { return VK_QUEUE_VIDEO_ENCODE_BIT_KHR; }

private:
    Engine& engine;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamilyIndex = 0;
    uint32_t videoDecodeQueueFamilyIndex = 0;
    uint32_t videoEncodeQueueFamilyIndex = 0;
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    VkQueue videoDecodeQueue = VK_NULL_HANDLE;
    VkQueue videoEncodeQueue = VK_NULL_HANDLE;
};
