#pragma once

#include <vulkan/vulkan.h>
#include <vector>
#include <utility>
#include <mutex>
#include <memory>

class RenderDevice;
class VmaAllocatorManager;

class BufferManager
{
public:
    BufferManager();
    ~BufferManager();

    // Non-copyable
    BufferManager(const BufferManager&) = delete;
    BufferManager& operator=(const BufferManager&) = delete;
    // Movable
    BufferManager(BufferManager&& other) noexcept;
    BufferManager& operator=(BufferManager&& other) noexcept;

    // Initialize with RenderDevice
    void initialize(RenderDevice* renderDevice);
    
    // Initialize with VMA support (preferred)
    void initialize(RenderDevice* renderDevice,
                    VkInstance instance,
                    VkPhysicalDevice physicalDevice,
                    VkDevice device);
    
    void shutdown();
    bool isInitialized() const { return renderDevice_ != nullptr; }
    bool hasVma() const { return vmaAllocator_ != nullptr; }

    // Buffer operations
    void createBuffer(VkDeviceSize size, 
                      VkBufferUsageFlags usage, 
                      VkMemoryPropertyFlags properties,
                      VkBuffer& buffer, 
                      VkDeviceMemory& bufferMemory);
    
    // Create buffer using VMA (returns allocation handle for later destruction)
    bool createBufferVma(VkDeviceSize size,
                         VkBufferUsageFlags usage,
                         VkBuffer& buffer,
                         void** allocationHandle);
    
    void destroyBufferVma(VkBuffer buffer, void* allocationHandle);

    void copyBuffer(VkBuffer srcBuffer, 
                    VkBuffer dstBuffer, 
                    VkDeviceSize size,
                    VkCommandPool commandPool, 
                    VkQueue graphicsQueue);
    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);
    VkShaderModule createShaderModule(const std::vector<char>& code);

    // Batch upload for staging buffer optimization
    void beginBatchUpload();
    void endBatchUpload();
    bool isInBatchUpload() const { return batchUploadDepth_ > 0; }
    void deferStagingBufferDestruction(VkBuffer buffer, VkDeviceMemory memory);

    // Direct command buffer access during batch
    VkCommandBuffer getActiveBatchCommandBuffer() const { return activeBatchCommandBuffer_; }
    void setActiveBatchCommandBuffer(VkCommandBuffer cmdBuffer) { activeBatchCommandBuffer_ = cmdBuffer; }

    // VMA access
    VmaAllocatorManager* getVmaAllocator() const { return vmaAllocator_.get(); }

private:
    RenderDevice* renderDevice_ = nullptr;
    std::unique_ptr<VmaAllocatorManager> vmaAllocator_;

    // Batch upload state
    int batchUploadDepth_ = 0;
    std::vector<std::pair<VkBuffer, VkDeviceMemory>> pendingStagingBuffers_;
    std::mutex batchMutex_;
    VkCommandBuffer activeBatchCommandBuffer_ = VK_NULL_HANDLE;
};
