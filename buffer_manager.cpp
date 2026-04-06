#include "buffer_manager.h"
#include "graphicsdevice.h"
#include "vma_allocator.h"

BufferManager::BufferManager() = default;

BufferManager::~BufferManager()
{
    shutdown();
}

BufferManager::BufferManager(BufferManager&& other) noexcept
    : renderDevice_(other.renderDevice_)
    , vmaAllocator_(std::move(other.vmaAllocator_))
    , batchUploadDepth_(other.batchUploadDepth_)
    , pendingStagingBuffers_(std::move(other.pendingStagingBuffers_))
    , activeBatchCommandBuffer_(other.activeBatchCommandBuffer_)
{
    other.renderDevice_ = nullptr;
    other.batchUploadDepth_ = 0;
    other.activeBatchCommandBuffer_ = VK_NULL_HANDLE;
}

BufferManager& BufferManager::operator=(BufferManager&& other) noexcept
{
    if (this != &other)
    {
        shutdown();
        
        renderDevice_ = other.renderDevice_;
        vmaAllocator_ = std::move(other.vmaAllocator_);
        batchUploadDepth_ = other.batchUploadDepth_;
        pendingStagingBuffers_ = std::move(other.pendingStagingBuffers_);
        activeBatchCommandBuffer_ = other.activeBatchCommandBuffer_;

        other.renderDevice_ = nullptr;
        other.batchUploadDepth_ = 0;
        other.activeBatchCommandBuffer_ = VK_NULL_HANDLE;
    }
    return *this;
}

void BufferManager::initialize(RenderDevice* renderDevice)
{
    renderDevice_ = renderDevice;
}

void BufferManager::initialize(RenderDevice* renderDevice,
                               VkInstance instance,
                               VkPhysicalDevice physicalDevice,
                               VkDevice device)
{
    renderDevice_ = renderDevice;
    
    // Initialize VMA
    vmaAllocator_ = std::make_unique<VmaAllocatorManager>();
    vmaAllocator_->initialize(instance, physicalDevice, device);
}

void BufferManager::shutdown()
{
    if (!renderDevice_)
    {
        return;
    }

    // Clean up any remaining staging buffers
    if (!pendingStagingBuffers_.empty())
    {
        VkDevice device = renderDevice_->getLogicalDevice();
        vkDeviceWaitIdle(device);
        
        for (auto& [buffer, memory] : pendingStagingBuffers_)
        {
            vkDestroyBuffer(device, buffer, nullptr);
            vkFreeMemory(device, memory, nullptr);
        }
        pendingStagingBuffers_.clear();
    }

    // Shutdown VMA
    if (vmaAllocator_)
    {
        vmaAllocator_->shutdown();
        vmaAllocator_.reset();
    }

    renderDevice_ = nullptr;
}

void BufferManager::createBuffer(VkDeviceSize size,
                                 VkBufferUsageFlags usage,
                                 VkMemoryPropertyFlags properties,
                                 VkBuffer& buffer,
                                 VkDeviceMemory& bufferMemory)
{
    renderDevice_->createBuffer(size, usage, properties, buffer, bufferMemory);
}

bool BufferManager::createBufferVma(VkDeviceSize size,
                                    VkBufferUsageFlags usage,
                                    VkBuffer& buffer,
                                    void** allocationHandle)
{
    if (!vmaAllocator_)
    {
        return false;
    }
    
    VmaAllocation allocation;
    bool result = vmaAllocator_->createBuffer(size, usage, VMA_MEMORY_USAGE_UNKNOWN, 
                                               buffer, allocation, nullptr);
    if (result && allocationHandle)
    {
        *allocationHandle = allocation;
    }
    return result;
}

void BufferManager::destroyBufferVma(VkBuffer buffer, void* allocationHandle)
{
    if (vmaAllocator_ && allocationHandle)
    {
        vmaAllocator_->destroyBuffer(buffer, static_cast<VmaAllocation>(allocationHandle));
    }
}

void BufferManager::copyBuffer(VkBuffer srcBuffer,
                               VkBuffer dstBuffer,
                               VkDeviceSize size,
                               VkCommandPool commandPool,
                               VkQueue graphicsQueue)
{
    renderDevice_->copyBuffer(srcBuffer, dstBuffer, size, commandPool, graphicsQueue);
}

uint32_t BufferManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    return renderDevice_->findMemoryType(typeFilter, properties);
}

VkShaderModule BufferManager::createShaderModule(const std::vector<char>& code)
{
    return renderDevice_->createShaderModule(code);
}

void BufferManager::beginBatchUpload()
{
    std::lock_guard<std::mutex> lock(batchMutex_);
    if (batchUploadDepth_++ == 0)
    {
        pendingStagingBuffers_.clear();
        activeBatchCommandBuffer_ = VK_NULL_HANDLE;
    }
}

void BufferManager::endBatchUpload()
{
    VkCommandBuffer commandBufferToSubmit = VK_NULL_HANDLE;
    std::vector<std::pair<VkBuffer, VkDeviceMemory>> stagingBuffersToDestroy;
    
    {
        std::lock_guard<std::mutex> lock(batchMutex_);
        if (--batchUploadDepth_ == 0)
        {
            commandBufferToSubmit = activeBatchCommandBuffer_;
            activeBatchCommandBuffer_ = VK_NULL_HANDLE;
            stagingBuffersToDestroy.swap(pendingStagingBuffers_);
        }
    }

    if (commandBufferToSubmit != VK_NULL_HANDLE)
    {
        renderDevice_->endSingleTimeCommands(commandBufferToSubmit);
    }

    if (!stagingBuffersToDestroy.empty())
    {
        VkDevice device = renderDevice_->getLogicalDevice();
        vkDeviceWaitIdle(device);

        for (auto& [buffer, memory] : stagingBuffersToDestroy)
        {
            vkDestroyBuffer(device, buffer, nullptr);
            vkFreeMemory(device, memory, nullptr);
        }
    }
}

void BufferManager::deferStagingBufferDestruction(VkBuffer buffer, VkDeviceMemory memory)
{
    std::lock_guard<std::mutex> lock(batchMutex_);
    if (batchUploadDepth_ > 0)
    {
        pendingStagingBuffers_.emplace_back(buffer, memory);
    }
    else
    {
        // Not in batch mode, destroy immediately
        VkDevice device = renderDevice_->getLogicalDevice();
        vkDestroyBuffer(device, buffer, nullptr);
        vkFreeMemory(device, memory, nullptr);
    }
}
