#include "light_manager.h"

#include <cstring>
#include <stdexcept>

LightManager::LightManager()
    : currentLight_(glm::vec3(0.0f, 0.0f, 1.0f),
                    glm::vec3(0.0f),
                    glm::vec3(1.0f))
{
}

LightManager::~LightManager()
{
    shutdown();
}

LightManager::LightManager(LightManager&& other) noexcept
    : device_(other.device_)
    , graphicsQueueFamilyIndex_(other.graphicsQueueFamilyIndex_)
    , memProperties_(other.memProperties_)
    , currentLight_(other.currentLight_)
    , lightUBO_(other.lightUBO_)
    , lightUBOMemory_(other.lightUBOMemory_)
    , lightUBOMapped_(other.lightUBOMapped_)
{
    other.device_ = VK_NULL_HANDLE;
    other.lightUBO_ = VK_NULL_HANDLE;
    other.lightUBOMemory_ = VK_NULL_HANDLE;
    other.lightUBOMapped_ = nullptr;
}

LightManager& LightManager::operator=(LightManager&& other) noexcept
{
    if (this != &other)
    {
        shutdown();
        
        device_ = other.device_;
        graphicsQueueFamilyIndex_ = other.graphicsQueueFamilyIndex_;
        memProperties_ = other.memProperties_;
        currentLight_ = other.currentLight_;
        lightUBO_ = other.lightUBO_;
        lightUBOMemory_ = other.lightUBOMemory_;
        lightUBOMapped_ = other.lightUBOMapped_;

        other.device_ = VK_NULL_HANDLE;
        other.lightUBO_ = VK_NULL_HANDLE;
        other.lightUBOMemory_ = VK_NULL_HANDLE;
        other.lightUBOMapped_ = nullptr;
    }
    return *this;
}

void LightManager::initialize(VkDevice device,
                              uint32_t graphicsQueueFamilyIndex,
                              VkPhysicalDeviceMemoryProperties memProperties)
{
    device_ = device;
    graphicsQueueFamilyIndex_ = graphicsQueueFamilyIndex;
    memProperties_ = memProperties;

    createBuffer(sizeof(LightUBOData),
                 VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                 VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    updateBuffer();
}

void LightManager::shutdown()
{
    if (device_ == VK_NULL_HANDLE)
    {
        return;
    }

    destroyBuffer();
    device_ = VK_NULL_HANDLE;
}

void LightManager::setLight(const Light& light)
{
    currentLight_ = light;
    updateBuffer();
}

void LightManager::createBuffer(VkDeviceSize size,
                                VkBufferUsageFlags usage,
                                VkMemoryPropertyFlags properties)
{
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    bufferInfo.queueFamilyIndexCount = 1;
    bufferInfo.pQueueFamilyIndices = &graphicsQueueFamilyIndex_;

    if (vkCreateBuffer(device_, &bufferInfo, nullptr, &lightUBO_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create light uniform buffer");
    }

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(device_, lightUBO_, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = findMemoryType(memRequirements.memoryTypeBits, properties);

    if (vkAllocateMemory(device_, &allocInfo, nullptr, &lightUBOMemory_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate light uniform buffer memory");
    }

    vkBindBufferMemory(device_, lightUBO_, lightUBOMemory_, 0);

    if (vkMapMemory(device_, lightUBOMemory_, 0, size, 0, &lightUBOMapped_) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to map light uniform buffer");
    }
}

void LightManager::destroyBuffer()
{
    if (lightUBOMapped_)
    {
        vkUnmapMemory(device_, lightUBOMemory_);
        lightUBOMapped_ = nullptr;
    }
    if (lightUBO_ != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(device_, lightUBO_, nullptr);
        lightUBO_ = VK_NULL_HANDLE;
    }
    if (lightUBOMemory_ != VK_NULL_HANDLE)
    {
        vkFreeMemory(device_, lightUBOMemory_, nullptr);
        lightUBOMemory_ = VK_NULL_HANDLE;
    }
}

void LightManager::updateBuffer()
{
    if (!lightUBOMapped_)
    {
        return;
    }

    LightUBOData ubo{};
    ubo.direction = glm::vec4(currentLight_.direction, 0.0f);
    ubo.ambient = glm::vec4(currentLight_.ambient, 0.0f);
    ubo.diffuse = glm::vec4(currentLight_.diffuse, 0.0f);

    std::memcpy(lightUBOMapped_, &ubo, sizeof(ubo));
}

uint32_t LightManager::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties)
{
    for (uint32_t i = 0; i < memProperties_.memoryTypeCount; i++)
    {
        if ((typeFilter & (1 << i)) &&
            (memProperties_.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }
    throw std::runtime_error("Failed to find suitable memory type for light buffer");
}
