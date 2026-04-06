#define VMA_IMPLEMENTATION
#include "vma_allocator.h"

#include <iostream>
#include <cstring>

VmaAllocatorManager::VmaAllocatorManager() = default;

VmaAllocatorManager::~VmaAllocatorManager()
{
    shutdown();
}

VmaAllocatorManager::VmaAllocatorManager(VmaAllocatorManager&& other) noexcept
    : allocator_(other.allocator_)
    , device_(other.device_)
{
    other.allocator_ = VK_NULL_HANDLE;
    other.device_ = VK_NULL_HANDLE;
}

VmaAllocatorManager& VmaAllocatorManager::operator=(VmaAllocatorManager&& other) noexcept
{
    if (this != &other)
    {
        shutdown();
        
        allocator_ = other.allocator_;
        device_ = other.device_;

        other.allocator_ = VK_NULL_HANDLE;
        other.device_ = VK_NULL_HANDLE;
    }
    return *this;
}

void VmaAllocatorManager::initialize(VkInstance instance,
                                     VkPhysicalDevice physicalDevice,
                                     VkDevice device,
                                     uint32_t vulkanApiVersion)
{
    device_ = device;

    VmaAllocatorCreateInfo allocatorInfo = {};
    allocatorInfo.vulkanApiVersion = vulkanApiVersion;
    allocatorInfo.physicalDevice = physicalDevice;
    allocatorInfo.device = device;
    allocatorInfo.instance = instance;
    
    // Enable helpful flags for debugging
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_EXT_MEMORY_BUDGET_BIT;

    VkResult result = vmaCreateAllocator(&allocatorInfo, &allocator_);
    if (result != VK_SUCCESS)
    {
        std::cerr << "Failed to create VMA allocator: " << result << std::endl;
        allocator_ = VK_NULL_HANDLE;
    }
}

void VmaAllocatorManager::shutdown()
{
    if (allocator_ != VK_NULL_HANDLE)
    {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
    device_ = VK_NULL_HANDLE;
}

bool VmaAllocatorManager::createBuffer(VkDeviceSize size,
                                       VkBufferUsageFlags usage,
                                       VmaMemoryUsage memoryUsage,
                                       VkBuffer& outBuffer,
                                       VmaAllocation& outAllocation,
                                       void** mappedData)
{
    VkBufferCreateInfo bufferInfo = {};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;
    if (mappedData)
    {
        allocInfo.flags = VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    VkResult result = vmaCreateBuffer(allocator_, &bufferInfo, &allocInfo, 
                                      &outBuffer, &outAllocation, nullptr);
    
    if (result != VK_SUCCESS)
    {
        std::cerr << "Failed to create VMA buffer: " << result << std::endl;
        return false;
    }

    if (mappedData)
    {
        VmaAllocationInfo allocationInfo;
        vmaGetAllocationInfo(allocator_, outAllocation, &allocationInfo);
        *mappedData = allocationInfo.pMappedData;
    }

    return true;
}

bool VmaAllocatorManager::createStagingBuffer(VkDeviceSize size,
                                              VkBuffer& outBuffer,
                                              VmaAllocation& outAllocation,
                                              void** mappedData)
{
    return createBuffer(size, 
                        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                        VMA_MEMORY_USAGE_CPU_ONLY,
                        outBuffer, 
                        outAllocation, 
                        mappedData);
}

bool VmaAllocatorManager::createDeviceLocalBuffer(VkDeviceSize size,
                                                  VkBufferUsageFlags usage,
                                                  VkBuffer& outBuffer,
                                                  VmaAllocation& outAllocation)
{
    return createBuffer(size,
                        usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                        VMA_MEMORY_USAGE_GPU_ONLY,
                        outBuffer,
                        outAllocation,
                        nullptr);
}

bool VmaAllocatorManager::createUniformBuffer(VkDeviceSize size,
                                              VkBuffer& outBuffer,
                                              VmaAllocation& outAllocation,
                                              void** mappedData)
{
    return createBuffer(size,
                        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        VMA_MEMORY_USAGE_CPU_TO_GPU,
                        outBuffer,
                        outAllocation,
                        mappedData);
}

void VmaAllocatorManager::destroyBuffer(VkBuffer buffer, VmaAllocation allocation)
{
    if (allocator_ != VK_NULL_HANDLE && buffer != VK_NULL_HANDLE)
    {
        vmaDestroyBuffer(allocator_, buffer, allocation);
    }
}

bool VmaAllocatorManager::createImage(const VkImageCreateInfo& imageInfo,
                                      VmaMemoryUsage memoryUsage,
                                      VkImage& outImage,
                                      VmaAllocation& outAllocation)
{
    VmaAllocationCreateInfo allocInfo = {};
    allocInfo.usage = memoryUsage;

    VkResult result = vmaCreateImage(allocator_, &imageInfo, &allocInfo,
                                     &outImage, &outAllocation, nullptr);
    
    if (result != VK_SUCCESS)
    {
        std::cerr << "Failed to create VMA image: " << result << std::endl;
        return false;
    }

    return true;
}

void VmaAllocatorManager::destroyImage(VkImage image, VmaAllocation allocation)
{
    if (allocator_ != VK_NULL_HANDLE && image != VK_NULL_HANDLE)
    {
        vmaDestroyImage(allocator_, image, allocation);
    }
}

bool VmaAllocatorManager::mapMemory(VmaAllocation allocation, void** data)
{
    VkResult result = vmaMapMemory(allocator_, allocation, data);
    return result == VK_SUCCESS;
}

void VmaAllocatorManager::unmapMemory(VmaAllocation allocation)
{
    vmaUnmapMemory(allocator_, allocation);
}

void VmaAllocatorManager::flushAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size)
{
    vmaFlushAllocation(allocator_, allocation, offset, size);
}

void VmaAllocatorManager::invalidateAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size)
{
    vmaInvalidateAllocation(allocator_, allocation, offset, size);
}

void VmaAllocatorManager::getHeapBudgets(VmaBudget* outBudgets, uint32_t heapCount)
{
    if (allocator_ != VK_NULL_HANDLE && outBudgets != nullptr)
    {
        vmaGetHeapBudgets(allocator_, outBudgets);
    }
}

void VmaAllocatorManager::printStatistics() const
{
    if (allocator_ == VK_NULL_HANDLE)
    {
        return;
    }

    VmaTotalStatistics stats;
    vmaCalculateStatistics(allocator_, &stats);

    std::cout << "=== VMA Statistics ===" << std::endl;
    std::cout << "Total allocations: " << stats.total.statistics.allocationCount << std::endl;
    std::cout << "Total bytes allocated: " << stats.total.statistics.allocationBytes << std::endl;
    std::cout << "Total blocks: " << stats.total.statistics.blockCount << std::endl;
    std::cout << "Total block bytes: " << stats.total.statistics.blockBytes << std::endl;
    std::cout << "Unused bytes: " << (stats.total.statistics.blockBytes - stats.total.statistics.allocationBytes) << std::endl;
}
