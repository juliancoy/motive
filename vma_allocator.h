#pragma once

#include <vulkan/vulkan.h>

// VMA configuration - include Vulkan headers first
#include "VulkanMemoryAllocator/include/vk_mem_alloc.h"

#include <memory>
#include <vector>

class RenderDevice;

// Wrapper for Vulkan Memory Allocator (VMA)
class VmaAllocatorManager
{
public:
    VmaAllocatorManager();
    ~VmaAllocatorManager();

    // Non-copyable
    VmaAllocatorManager(const VmaAllocatorManager&) = delete;
    VmaAllocatorManager& operator=(const VmaAllocatorManager&) = delete;
    // Movable
    VmaAllocatorManager(VmaAllocatorManager&& other) noexcept;
    VmaAllocatorManager& operator=(VmaAllocatorManager&& other) noexcept;

    void initialize(VkInstance instance,
                    VkPhysicalDevice physicalDevice,
                    VkDevice device,
                    uint32_t vulkanApiVersion = VK_API_VERSION_1_2);
    void shutdown();
    bool isInitialized() const { return allocator_ != VK_NULL_HANDLE; }

    // Access to underlying allocator
    VmaAllocator getAllocator() const { return allocator_; }

    // Convenience methods for buffer creation
    bool createBuffer(VkDeviceSize size,
                      VkBufferUsageFlags usage,
                      VmaMemoryUsage memoryUsage,
                      VkBuffer& outBuffer,
                      VmaAllocation& outAllocation,
                      void** mappedData = nullptr);
    
    bool createStagingBuffer(VkDeviceSize size,
                             VkBuffer& outBuffer,
                             VmaAllocation& outAllocation,
                             void** mappedData);

    bool createDeviceLocalBuffer(VkDeviceSize size,
                                 VkBufferUsageFlags usage,
                                 VkBuffer& outBuffer,
                                 VmaAllocation& outAllocation);

    bool createUniformBuffer(VkDeviceSize size,
                             VkBuffer& outBuffer,
                             VmaAllocation& outAllocation,
                             void** mappedData);

    void destroyBuffer(VkBuffer buffer, VmaAllocation allocation);

    // Convenience methods for image creation
    bool createImage(const VkImageCreateInfo& imageInfo,
                     VmaMemoryUsage memoryUsage,
                     VkImage& outImage,
                     VmaAllocation& outAllocation);

    void destroyImage(VkImage image, VmaAllocation allocation);

    // Memory mapping helpers
    bool mapMemory(VmaAllocation allocation, void** data);
    void unmapMemory(VmaAllocation allocation);
    void flushAllocation(VmaAllocation allocation, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
    void invalidateAllocation(VmaAllocation allocation, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    // Statistics
    void getHeapBudgets(VmaBudget* outBudgets, uint32_t heapCount);
    void printStatistics() const;

private:
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
};
