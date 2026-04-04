#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <cstring>
#include <iostream>
#include <stdexcept>

#include "display.h"
#include "engine.h"

namespace
{
void logBitmap(const glyph::OverlayBitmap& bitmap)
{
    static int lastWidth = 0;
    static int lastHeight = 0;
    if (bitmap.width != lastWidth || bitmap.height != lastHeight || bitmap.pixels.empty())
    {
        std::cout << "[Display] Overlay bitmap generated. "
                  << "Width: " << bitmap.width << ", Height: " << bitmap.height
                  << ", Pixels empty: " << (bitmap.pixels.empty() ? "yes" : "no") << std::endl;
        lastWidth = bitmap.width;
        lastHeight = bitmap.height;
    }
}
} // namespace

void Display::createOverlayBuffer(VkDeviceSize size)
{
    if (size == 0)
    {
        return;
    }

    VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bufferInfo.size = size;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(engine->logicalDevice, &bufferInfo, nullptr, &overlayResources.stagingBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create overlay staging buffer");
    }

    VkMemoryRequirements memRequirements{};
    vkGetBufferMemoryRequirements(engine->logicalDevice, overlayResources.stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine->findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &overlayResources.stagingMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate overlay staging buffer memory");
    }

    vkBindBufferMemory(engine->logicalDevice, overlayResources.stagingBuffer, overlayResources.stagingMemory, 0);

    if (vkMapMemory(engine->logicalDevice, overlayResources.stagingMemory, 0, size, 0, &overlayResources.mapped) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to map overlay staging buffer memory");
    }

    overlayResources.bufferSize = size;
}

void Display::destroyOverlayBuffer()
{
    if (!engine || engine->logicalDevice == VK_NULL_HANDLE)
    {
        return;
    }

    vkDeviceWaitIdle(engine->logicalDevice);

    if (overlayResources.mapped)
    {
        vkUnmapMemory(engine->logicalDevice, overlayResources.stagingMemory);
        overlayResources.mapped = nullptr;
    }
    if (overlayResources.stagingBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, overlayResources.stagingBuffer, nullptr);
        overlayResources.stagingBuffer = VK_NULL_HANDLE;
    }
    if (overlayResources.stagingMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, overlayResources.stagingMemory, nullptr);
        overlayResources.stagingMemory = VK_NULL_HANDLE;
    }
    overlayResources.bufferSize = 0;
    overlayResources.width = 0;
    overlayResources.height = 0;
}

void Display::updateOverlayBitmap(float fps)
{
    const uint32_t referenceWidth = static_cast<uint32_t>(std::max(1, width));
    const uint32_t referenceHeight = static_cast<uint32_t>(std::max(1, height));
    glyph::OverlayBitmap bitmap = glyph::buildFrameRateOverlay(referenceWidth, referenceHeight, fps);

    if (bitmap.width == 0 || bitmap.height == 0 || bitmap.pixels.empty())
    {
        logBitmap(bitmap);
        overlayResources.width = 0;
        overlayResources.height = 0;
        return;
    }

    VkDeviceSize requiredSize = static_cast<VkDeviceSize>(bitmap.width) * bitmap.height * 4;
    if (requiredSize > overlayResources.bufferSize)
    {
        destroyOverlayBuffer();
        createOverlayBuffer(requiredSize);
    }

    if (!overlayResources.mapped)
    {
        overlayResources.width = 0;
        overlayResources.height = 0;
        return;
    }

    logBitmap(bitmap);
    std::memcpy(overlayResources.mapped, bitmap.pixels.data(), static_cast<size_t>(requiredSize));
    overlayResources.width = bitmap.width;
    overlayResources.height = bitmap.height;
    overlayResources.offsetX = bitmap.offsetX;
    overlayResources.offsetY = bitmap.offsetY;
}

void Display::recordOverlayCopy(VkCommandBuffer commandBuffer, uint32_t imageIndex)
{
    if (overlayResources.width == 0 || overlayResources.height == 0)
    {
        return;
    }
    if (overlayResources.stagingBuffer == VK_NULL_HANDLE || imageIndex >= swapchainImages.size())
    {
        return;
    }

    VkImage targetImage = swapchainImages[imageIndex];

    VkImageMemoryBarrier toTransfer{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = targetImage;
    toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toTransfer.subresourceRange.baseMipLevel = 0;
    toTransfer.subresourceRange.levelCount = 1;
    toTransfer.subresourceRange.baseArrayLayer = 0;
    toTransfer.subresourceRange.layerCount = 1;
    toTransfer.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {static_cast<int32_t>(overlayResources.offsetX), static_cast<int32_t>(overlayResources.offsetY), 0};
    region.imageExtent = {overlayResources.width, overlayResources.height, 1};

    vkCmdCopyBufferToImage(
        commandBuffer,
        overlayResources.stagingBuffer,
        targetImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    VkImageMemoryBarrier toPresent{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    toPresent.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toPresent.image = targetImage;
    toPresent.subresourceRange = toTransfer.subresourceRange;
    toPresent.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toPresent.dstAccessMask = 0;

    vkCmdPipelineBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &toPresent);
}
