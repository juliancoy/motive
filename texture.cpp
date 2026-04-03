
#include "texture.h"
#include "engine.h"
#include "model.h"
#include <QImage>
#include <stdexcept>
#include <iostream>

namespace
{
void destroyImageResources(VkDevice device, VkImage &image, VkDeviceMemory &memory, VkImageView &view)
{
    if (view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, view, nullptr);
        view = VK_NULL_HANDLE;
    }
    if (image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, image, nullptr);
        image = VK_NULL_HANDLE;
    }
    if (memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, memory, nullptr);
        memory = VK_NULL_HANDLE;
    }
}

void createImageResources(Primitive *primitive,
                          uint32_t width,
                          uint32_t height,
                          VkFormat format,
                          VkImage &outImage,
                          VkDeviceMemory &outMemory)
{
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = format;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(primitive->engine->logicalDevice, &imageInfo, nullptr, &outImage) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create image.");
    }

    VkMemoryRequirements memRequirements{};
    vkGetImageMemoryRequirements(primitive->engine->logicalDevice, outImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = primitive->engine->findMemoryType(
        memRequirements.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(primitive->engine->logicalDevice, &allocInfo, nullptr, &outMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate image memory.");
    }

    vkBindImageMemory(primitive->engine->logicalDevice, outImage, outMemory, 0);
}

VkImageView createImageView(Primitive *primitive, VkImage image, VkFormat format)
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    VkImageView imageView = VK_NULL_HANDLE;
    if (vkCreateImageView(primitive->engine->logicalDevice, &viewInfo, nullptr, &imageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create image view.");
    }
    return imageView;
}

void copyBufferToImage(Primitive *primitive,
                       VkBuffer stagingBuffer,
                       VkImage targetImage,
                       VkImageLayout oldLayout,
                       uint32_t width,
                       uint32_t height,
                       uint32_t bufferRowLength = 0)
{
    VkCommandBuffer cmdBuffer = primitive->engine->beginSingleTimeCommands();

    VkImageMemoryBarrier beginBarrier{};
    beginBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    beginBarrier.oldLayout = oldLayout;
    beginBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    beginBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarrier.image = targetImage;
    beginBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    beginBarrier.subresourceRange.baseMipLevel = 0;
    beginBarrier.subresourceRange.levelCount = 1;
    beginBarrier.subresourceRange.baseArrayLayer = 0;
    beginBarrier.subresourceRange.layerCount = 1;
    beginBarrier.srcAccessMask = (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? 0 : VK_ACCESS_SHADER_READ_BIT;
    beginBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED) ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &beginBarrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = bufferRowLength;  // Use provided row length (0 = tightly packed)
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(
        cmdBuffer,
        stagingBuffer,
        targetImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    VkImageMemoryBarrier endBarrier{};
    endBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    endBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    endBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    endBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    endBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    endBarrier.image = targetImage;
    endBarrier.subresourceRange = beginBarrier.subresourceRange;
    endBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    endBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &endBarrier);

    primitive->engine->endSingleTimeCommands(cmdBuffer);
}

void ensureInactiveTextureResources(Primitive *primitive, uint32_t width, uint32_t height, VkFormat format)
{
    if (!primitive->textureDoubleBuffered)
    {
        return;
    }

    if (primitive->textureImageInactive != VK_NULL_HANDLE &&
        primitive->textureWidth == width &&
        primitive->textureHeight == height &&
        primitive->textureFormat == format)
    {
        return;
    }

    destroyImageResources(primitive->engine->logicalDevice,
                          primitive->textureImageInactive,
                          primitive->textureImageMemoryInactive,
                          primitive->textureImageViewInactive);
    primitive->textureInactiveInitialized = false;

    if (width == 0 || height == 0 || format == VK_FORMAT_UNDEFINED)
    {
        return;
    }

    createImageResources(primitive, width, height, format,
                         primitive->textureImageInactive,
                         primitive->textureImageMemoryInactive);
    primitive->textureImageViewInactive = createImageView(primitive, primitive->textureImageInactive, format);
}

void ensureInactiveChromaResources(Primitive *primitive, uint32_t width, uint32_t height)
{
    if (!primitive->textureDoubleBuffered)
    {
        return;
    }

    if (primitive->chromaImageInactive != VK_NULL_HANDLE &&
        primitive->chromaWidth == width &&
        primitive->chromaHeight == height)
    {
        return;
    }

    destroyImageResources(primitive->engine->logicalDevice,
                          primitive->chromaImageInactive,
                          primitive->chromaImageMemoryInactive,
                          primitive->chromaImageViewInactive);
    primitive->chromaInactiveInitialized = false;

    if (width == 0 || height == 0)
    {
        return;
    }

    createImageResources(primitive, width, height, VK_FORMAT_R8G8_UNORM,
                         primitive->chromaImageInactive,
                         primitive->chromaImageMemoryInactive);
    primitive->chromaImageViewInactive = createImageView(primitive, primitive->chromaImageInactive, VK_FORMAT_R8G8_UNORM);
}

void assignSharedTextureResources(Primitive* primitive, const std::shared_ptr<SharedTextureResources>& shared)
{
    primitive->sharedTextureResources = shared;
    primitive->textureImage = shared ? shared->textureImage : VK_NULL_HANDLE;
    primitive->textureImageMemory = shared ? shared->textureImageMemory : VK_NULL_HANDLE;
    primitive->textureImageView = shared ? shared->textureImageView : VK_NULL_HANDLE;
    primitive->textureSampler = shared ? shared->textureSampler : VK_NULL_HANDLE;
    primitive->textureWidth = shared ? shared->textureWidth : 0;
    primitive->textureHeight = shared ? shared->textureHeight : 0;
    primitive->textureFormat = shared ? shared->textureFormat : VK_FORMAT_UNDEFINED;
}
} // namespace

SharedTextureResources::~SharedTextureResources()
{
    if (!engine)
    {
        return;
    }
    if (textureImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, textureImageView, nullptr);
    }
    if (textureImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, textureImage, nullptr);
    }
    if (textureImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, textureImageMemory, nullptr);
    }
    if (textureSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(engine->logicalDevice, textureSampler, nullptr);
    }
}

Material::Material(Engine* engine, Model* model, Primitive* primitive, tinygltf::Material /*tmaterial*/){
    
}

void Primitive::createTextureSampler()
{
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;

    samplerInfo.anisotropyEnable = VK_TRUE;
    samplerInfo.maxAnisotropy = engine->props.limits.maxSamplerAnisotropy;

    samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    samplerInfo.unnormalizedCoordinates = VK_FALSE;
    samplerInfo.compareEnable = VK_FALSE;
    samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.mipLodBias = 0.0f;
    samplerInfo.minLod = 0.0f;
    samplerInfo.maxLod = 0.0f;

    if (vkCreateSampler(engine->logicalDevice, &samplerInfo, nullptr, &textureSampler) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create texture sampler!");
    }
}

void Primitive::createTextureResources()
{
    createTextureSampler();
    createDefaultTexture();
    finalizeTextureResources();
}

void Primitive::createTextureResources(const tinygltf::Model* model, const tinygltf::Primitive& tprimitive)
{
    Model* owningModel = mesh ? mesh->model : nullptr;
    if (owningModel && tprimitive.material >= 0)
    {
        auto cacheIt = owningModel->gltfMaterialTextureCache.find(tprimitive.material);
        if (cacheIt != owningModel->gltfMaterialTextureCache.end())
        {
            if (auto shared = cacheIt->second.lock())
            {
                const auto& material = model->materials[tprimitive.material];
                alphaCutoff = static_cast<float>(material.alphaCutoff);
                if (material.alphaMode == "MASK")
                {
                    alphaMode = PrimitiveAlphaMode::Mask;
                }
                else if (material.alphaMode == "BLEND")
                {
                    alphaMode = PrimitiveAlphaMode::Blend;
                }
                else
                {
                    alphaMode = PrimitiveAlphaMode::Opaque;
                }

                assignSharedTextureResources(this, shared);
                finalizeTextureResources();
                return;
            }
        }
    }

    createTextureSampler();
    bool loaded = createTextureFromGLTF(model, tprimitive);
    if (!loaded)
    {
        createDefaultTexture();
    }
    if (owningModel && tprimitive.material >= 0 &&
        textureImage != VK_NULL_HANDLE &&
        textureImageView != VK_NULL_HANDLE &&
        textureSampler != VK_NULL_HANDLE)
    {
        auto shared = std::make_shared<SharedTextureResources>();
        shared->engine = engine;
        shared->textureImage = textureImage;
        shared->textureImageMemory = textureImageMemory;
        shared->textureImageView = textureImageView;
        shared->textureSampler = textureSampler;
        shared->textureWidth = textureWidth;
        shared->textureHeight = textureHeight;
        shared->textureFormat = textureFormat;
        owningModel->gltfMaterialTextureCache[tprimitive.material] = shared;
        assignSharedTextureResources(this, shared);
    }
    finalizeTextureResources();
}

void Primitive::createTextureImageView()
{
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = (textureFormat == VK_FORMAT_UNDEFINED) ? VK_FORMAT_R8G8B8A8_SRGB : textureFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(engine->logicalDevice, &viewInfo, nullptr, &textureImageView) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create texture image view!");
    }
}

void Primitive::createDefaultTexture()
{
    const uint32_t width = 1;
    const uint32_t height = 1;
    const uint32_t pixel = 0xFFFFFFFFu; // White RGBA
    createTextureFromPixelData(&pixel, sizeof(pixel), width, height, VK_FORMAT_R8G8B8A8_SRGB);
}

void Primitive::createTextureFromPixelData(const void* pixelData, size_t dataSize, uint32_t width, uint32_t height, VkFormat format, uint32_t rowStrideBytes)
{
    if (!pixelData || dataSize == 0 || width == 0 || height == 0)
    {
        throw std::runtime_error("Invalid pixel data for texture creation.");
    }

    usesYuvTexture = false;
    yuvTextureFormat = PrimitiveYuvFormat::None;
    yuvChromaDivX = 1;
    yuvChromaDivY = 1;
    yuvBitDepth = 8;
    yuvColorSpace = 0;
    yuvColorRange = 0;
    
    // Use provided row stride for preview image, or assume tightly packed
    const int bytesPerLine = rowStrideBytes > 0 ? static_cast<int>(rowStrideBytes) : static_cast<int>(width * 4);
    texturePreviewImage = QImage(reinterpret_cast<const uchar*>(pixelData),
                                 static_cast<int>(width),
                                 static_cast<int>(height),
                                 bytesPerLine,
                                 QImage::Format_RGBA8888).copy();

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    stagingBufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    stagingBufferInfo.size = static_cast<VkDeviceSize>(dataSize);
    stagingBufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    stagingBufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(engine->logicalDevice, &stagingBufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create staging buffer for default texture!");
    }

    // Allocate staging buffer memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(engine->logicalDevice, stagingBuffer, &memRequirements);

    stagingAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    stagingAllocInfo.allocationSize = memRequirements.size;
    stagingAllocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits,
                                                              VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(engine->logicalDevice, &stagingAllocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate staging buffer memory!");
    }

    vkBindBufferMemory(engine->logicalDevice, stagingBuffer, stagingBufferMemory, 0);

    // Copy texture data to staging buffer
    void *data;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, stagingBufferInfo.size, 0, &data);
    memcpy(data, pixelData, static_cast<size_t>(stagingBufferInfo.size));
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    // Create texture image
    textureImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    textureImageInfo.imageType = VK_IMAGE_TYPE_2D;
    textureImageInfo.extent.width = width;
    textureImageInfo.extent.height = height;
    textureImageInfo.extent.depth = 1;
    textureImageInfo.mipLevels = 1;
    textureImageInfo.arrayLayers = 1;
    textureImageInfo.format = format;
    textureImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    textureImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    textureImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    textureImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    textureImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(engine->logicalDevice, &textureImageInfo, nullptr, &textureImage) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create default texture image!");
    }

    // Allocate texture image memory
    vkGetImageMemoryRequirements(engine->logicalDevice, textureImage, &memRequirements);

    stagingAllocInfo.allocationSize = memRequirements.size;
    stagingAllocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits,
                                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(engine->logicalDevice, &stagingAllocInfo, nullptr, &textureImageMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate texture image memory!");
    }

    vkBindImageMemory(engine->logicalDevice, textureImage, textureImageMemory, 0);

    // Transition image layout for copying
    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();
    imageBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    imageBarrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    imageBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = textureImage;
    imageBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageBarrier.subresourceRange.baseMipLevel = 0;
    imageBarrier.subresourceRange.levelCount = 1;
    imageBarrier.subresourceRange.baseArrayLayer = 0;
    imageBarrier.subresourceRange.layerCount = 1;
    imageBarrier.srcAccessMask = 0;
    imageBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &imageBarrier);

    // Copy buffer to image
    // NOTE: We set bufferRowLength to 0 (tightly packed) because:
    // 1. The staging buffer is filled with memcpy of sizeInBytes which includes any padding
    // 2. QImage's bytesPerLine may include padding for alignment
    // 3. Setting to 0 tells Vulkan to calculate stride as width * bytesPerPixel
    // If there's actual padding in the source data, we would need to set this correctly,
    // but currently we're copying the padded data as-is.
    imageCopyRegion.bufferOffset = 0;
    imageCopyRegion.bufferRowLength = 0;  // 0 = tightly packed (width * 4 bytes)
    imageCopyRegion.bufferImageHeight = 0;
    imageCopyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    imageCopyRegion.imageSubresource.mipLevel = 0;
    imageCopyRegion.imageSubresource.baseArrayLayer = 0;
    imageCopyRegion.imageSubresource.layerCount = 1;
    imageCopyRegion.imageOffset = {0, 0, 0};
    imageCopyRegion.imageExtent = {width, height, 1};

    vkCmdCopyBufferToImage(
        cmdBuffer,
        stagingBuffer,
        textureImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &imageCopyRegion);

    // Transition image layout to shader read
    shaderBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    shaderBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    shaderBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    shaderBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    shaderBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    shaderBarrier.image = textureImage;
    shaderBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    shaderBarrier.subresourceRange.baseMipLevel = 0;
    shaderBarrier.subresourceRange.levelCount = 1;
    shaderBarrier.subresourceRange.baseArrayLayer = 0;
    shaderBarrier.subresourceRange.layerCount = 1;
    shaderBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    shaderBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &shaderBarrier);

    engine->endSingleTimeCommands(cmdBuffer);

    if (textureDoubleBuffered)
    {
        ensureInactiveTextureResources(this, width, height, format);
        if (textureImageInactive != VK_NULL_HANDLE)
        {
            // Use 0 for tightly packed data (see note above)
            copyBufferToImage(this, stagingBuffer, textureImageInactive, VK_IMAGE_LAYOUT_UNDEFINED, width, height, 0);
            textureInactiveInitialized = true;
        }
    }

    textureWidth = width;
    textureHeight = height;
    textureFormat = format;

    // Cleanup staging resources (deferred if in batch mode)
    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);
}

void Primitive::updateTextureFromPixelData(const void* pixelData, size_t dataSize, uint32_t width, uint32_t height, VkFormat format, uint32_t rowStrideBytes)
{
    if (!pixelData || dataSize == 0 || width == 0 || height == 0)
    {
        throw std::runtime_error("Invalid pixel data for texture update.");
    }

    usesYuvTexture = false;
    yuvColorSpace = 0;
    yuvColorRange = 0;
    
    const int bytesPerLine = rowStrideBytes > 0 ? static_cast<int>(rowStrideBytes) : static_cast<int>(width * 4);
    texturePreviewImage = QImage(reinterpret_cast<const uchar*>(pixelData),
                                 static_cast<int>(width),
                                 static_cast<int>(height),
                                 bytesPerLine,
                                 QImage::Format_RGBA8888).copy();

    const bool needsRecreate = (textureImage == VK_NULL_HANDLE) ||
                               (width != textureWidth) ||
                               (height != textureHeight) ||
                               (format != textureFormat);

    if (needsRecreate)
    {
        // Clean up existing texture resources
        if (textureImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(engine->logicalDevice, textureImageView, nullptr);
            textureImageView = VK_NULL_HANDLE;
        }
        if (textureImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(engine->logicalDevice, textureImage, nullptr);
            textureImage = VK_NULL_HANDLE;
        }
        if (textureImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(engine->logicalDevice, textureImageMemory, nullptr);
            textureImageMemory = VK_NULL_HANDLE;
        }
        
        // Create new texture with the updated data
        createTextureFromPixelData(pixelData, dataSize, width, height, format, rowStrideBytes);
        createTextureImageView();
        updateDescriptorSet();
        return;
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    engine->createBuffer(
        static_cast<VkDeviceSize>(dataSize),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingMemory);

    void *mapped = nullptr;
    vkMapMemory(engine->logicalDevice, stagingMemory, 0, static_cast<VkDeviceSize>(dataSize), 0, &mapped);
    memcpy(mapped, pixelData, dataSize);
    vkUnmapMemory(engine->logicalDevice, stagingMemory);

    VkImage targetImage = textureDoubleBuffered ? textureImageInactive : textureImage;
    VkImageLayout oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    if (textureDoubleBuffered)
    {
        ensureInactiveTextureResources(this, width, height, format);
        oldLayout = textureInactiveInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    }

    // Use 0 for tightly packed data (see note in createTextureFromPixelData)
    copyBufferToImage(this, stagingBuffer, targetImage, oldLayout, width, height, 0);

    if (textureDoubleBuffered)
    {
        textureInactiveInitialized = true;
        std::swap(textureImage, textureImageInactive);
        std::swap(textureImageMemory, textureImageMemoryInactive);
        std::swap(textureImageView, textureImageViewInactive);
        updateDescriptorSet();
    }

    engine->deferStagingBufferDestruction(stagingBuffer, stagingMemory);
}

void Primitive::updateTextureFromNV12(const uint8_t* nv12Data, size_t dataSize, uint32_t width, uint32_t height)
{
    if (!nv12Data || dataSize == 0 || width == 0 || height == 0)
    {
        throw std::runtime_error("Invalid NV12 data for texture update.");
    }

    const size_t lumaSize = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (dataSize < lumaSize)
    {
        throw std::runtime_error("Insufficient NV12 data for Y plane.");
    }

    const uint32_t chromaWidthLocal = width / 2;
    const uint32_t chromaHeightLocal = height / 2;
    if (chromaWidthLocal == 0 || chromaHeightLocal == 0)
    {
        throw std::runtime_error("NV12 chroma plane dimensions invalid.");
    }

    const size_t expectedUvSize = static_cast<size_t>(chromaWidthLocal) * static_cast<size_t>(chromaHeightLocal) * 2;
    if ((dataSize - lumaSize) < expectedUvSize)
    {
        throw std::runtime_error("Insufficient NV12 data for UV plane.");
    }

    const uint8_t* uvPlane = nv12Data + lumaSize;

    updateTextureFromPixelData(nv12Data, lumaSize, width, height, VK_FORMAT_R8_UNORM);
    updateChromaPlaneTexture(uvPlane, expectedUvSize, chromaWidthLocal, chromaHeightLocal, VK_FORMAT_R8G8_UNORM);

    usesYuvTexture = true;
    yuvTextureFormat = PrimitiveYuvFormat::NV12;
    yuvChromaDivX = 2;
    yuvChromaDivY = 2;
    yuvBitDepth = 8;
    updateDescriptorSet();
}

void Primitive::updateTextureFromPlanarYuv(const uint8_t* yPlane,
                                           size_t yPlaneBytes,
                                           uint32_t width,
                                           uint32_t height,
                                           const uint8_t* uvPlane,
                                           size_t uvPlaneBytes,
                                           uint32_t chromaWidthLocal,
                                           uint32_t chromaHeightLocal,
                                           bool sixteenBit,
                                           PrimitiveYuvFormat formatTag)
{
    if (!yPlane || !uvPlane || yPlaneBytes == 0 || uvPlaneBytes == 0 || width == 0 || height == 0)
    {
        throw std::runtime_error("Invalid planar YUV data for texture update.");
    }

    const VkFormat lumaFormat = sixteenBit ? VK_FORMAT_R16_UNORM : VK_FORMAT_R8_UNORM;
    const VkFormat chromaFormatLocal = sixteenBit ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R8G8_UNORM;

    updateTextureFromPixelData(yPlane, yPlaneBytes, width, height, lumaFormat);
    updateChromaPlaneTexture(uvPlane, uvPlaneBytes, chromaWidthLocal, chromaHeightLocal, chromaFormatLocal);

    usesYuvTexture = true;
    yuvTextureFormat = formatTag;
    const uint32_t divX = chromaWidthLocal > 0 ? std::max(1u, width / chromaWidthLocal) : 1u;
    const uint32_t divY = chromaHeightLocal > 0 ? std::max(1u, height / chromaHeightLocal) : 1u;
    yuvChromaDivX = divX;
    yuvChromaDivY = divY;
    yuvBitDepth = sixteenBit ? 16u : 8u;
    updateDescriptorSet();
}

void Primitive::updateChromaPlaneTexture(const void* pixelData,
                                         size_t dataSize,
                                         uint32_t width,
                                         uint32_t height,
                                         VkFormat format)
{
    if (!pixelData || dataSize == 0 || width == 0 || height == 0)
    {
        throw std::runtime_error("Invalid chroma data for texture update.");
    }

    const bool needsRecreate = (chromaImage == VK_NULL_HANDLE) ||
                               (width != chromaWidth) ||
                               (height != chromaHeight) ||
                               (chromaFormat != format);

    if (needsRecreate)
    {
        if (chromaImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(engine->logicalDevice, chromaImageView, nullptr);
            chromaImageView = VK_NULL_HANDLE;
        }
        if (chromaImage != VK_NULL_HANDLE)
        {
            vkDestroyImage(engine->logicalDevice, chromaImage, nullptr);
            chromaImage = VK_NULL_HANDLE;
        }
        if (chromaImageMemory != VK_NULL_HANDLE)
        {
            vkFreeMemory(engine->logicalDevice, chromaImageMemory, nullptr);
            chromaImageMemory = VK_NULL_HANDLE;
        }

        VkImageCreateInfo chromaImageInfo{};
        chromaImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        chromaImageInfo.imageType = VK_IMAGE_TYPE_2D;
        chromaImageInfo.extent = {width, height, 1};
        chromaImageInfo.mipLevels = 1;
        chromaImageInfo.arrayLayers = 1;
        chromaImageInfo.format = format;
        chromaImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        chromaImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        chromaImageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        chromaImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        chromaImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (vkCreateImage(engine->logicalDevice, &chromaImageInfo, nullptr, &chromaImage) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create chroma image.");
        }

        VkMemoryRequirements memRequirements{};
        vkGetImageMemoryRequirements(engine->logicalDevice, chromaImage, &memRequirements);

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits,
                                                           VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

        if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &chromaImageMemory) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to allocate chroma image memory.");
        }

        vkBindImageMemory(engine->logicalDevice, chromaImage, chromaImageMemory, 0);
    }

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    engine->createBuffer(
        static_cast<VkDeviceSize>(dataSize),
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingMemory);

    void* mapped = nullptr;
    vkMapMemory(engine->logicalDevice, stagingMemory, 0, static_cast<VkDeviceSize>(dataSize), 0, &mapped);
    memcpy(mapped, pixelData, dataSize);
    vkUnmapMemory(engine->logicalDevice, stagingMemory);

    if (needsRecreate)
    {
        copyBufferToImage(this, stagingBuffer, chromaImage, VK_IMAGE_LAYOUT_UNDEFINED, width, height);
        if (textureDoubleBuffered)
        {
            ensureInactiveChromaResources(this, width, height);
            if (chromaImageInactive != VK_NULL_HANDLE)
            {
                copyBufferToImage(this, stagingBuffer, chromaImageInactive, VK_IMAGE_LAYOUT_UNDEFINED, width, height);
                chromaInactiveInitialized = true;
            }
        }
    }
    else
    {
        VkImage targetChromaImage = textureDoubleBuffered ? chromaImageInactive : chromaImage;
        VkImageLayout chromaOldLayout = textureDoubleBuffered
                                            ? (chromaInactiveInitialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED)
                                            : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        if (textureDoubleBuffered)
        {
            ensureInactiveChromaResources(this, width, height);
        }

        copyBufferToImage(this, stagingBuffer, targetChromaImage, chromaOldLayout, width, height);

        if (textureDoubleBuffered)
        {
            chromaInactiveInitialized = true;
            std::swap(chromaImage, chromaImageInactive);
            std::swap(chromaImageMemory, chromaImageMemoryInactive);
            std::swap(chromaImageView, chromaImageViewInactive);
            updateDescriptorSet();
        }
    }

    engine->deferStagingBufferDestruction(stagingBuffer, stagingMemory);

    chromaWidth = width;
    chromaHeight = height;
    chromaFormat = format;

    if (chromaImageView == VK_NULL_HANDLE || needsRecreate)
    {
        if (chromaImageView != VK_NULL_HANDLE)
        {
            vkDestroyImageView(engine->logicalDevice, chromaImageView, nullptr);
        }

        VkImageViewCreateInfo chromaViewInfo{};
        chromaViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        chromaViewInfo.image = chromaImage;
        chromaViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        chromaViewInfo.format = format;
        chromaViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        chromaViewInfo.subresourceRange.baseMipLevel = 0;
        chromaViewInfo.subresourceRange.levelCount = 1;
        chromaViewInfo.subresourceRange.baseArrayLayer = 0;
        chromaViewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(engine->logicalDevice, &chromaViewInfo, nullptr, &chromaImageView) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create chroma image view.");
        }
    }
}

bool Primitive::createTextureFromGLTF(const tinygltf::Model* model, const tinygltf::Primitive& tprimitive)
{
    if (!model || tprimitive.material < 0 || tprimitive.material >= static_cast<int>(model->materials.size()))
    {
        return false;
    }

    const auto& material = model->materials[tprimitive.material];
    const auto& baseColorFactor = material.pbrMetallicRoughness.baseColorFactor;
    int textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
    alphaCutoff = static_cast<float>(material.alphaCutoff);
    if (material.alphaMode == "MASK")
    {
        alphaMode = PrimitiveAlphaMode::Mask;
    }
    else if (material.alphaMode == "BLEND")
    {
        alphaMode = PrimitiveAlphaMode::Blend;
    }
    else
    {
        alphaMode = PrimitiveAlphaMode::Opaque;
    }

    auto applyBaseColorFactor = [&](std::vector<uint8_t>& rgbaData, size_t pixelCount)
    {
        const float factorR = baseColorFactor.size() > 0 ? static_cast<float>(baseColorFactor[0]) : 1.0f;
        const float factorG = baseColorFactor.size() > 1 ? static_cast<float>(baseColorFactor[1]) : 1.0f;
        const float factorB = baseColorFactor.size() > 2 ? static_cast<float>(baseColorFactor[2]) : 1.0f;
        const float factorA = baseColorFactor.size() > 3 ? static_cast<float>(baseColorFactor[3]) : 1.0f;

        for (size_t i = 0; i < pixelCount; ++i)
        {
            rgbaData[i * 4 + 0] = static_cast<uint8_t>(std::clamp((rgbaData[i * 4 + 0] / 255.0f) * factorR, 0.0f, 1.0f) * 255.0f);
            rgbaData[i * 4 + 1] = static_cast<uint8_t>(std::clamp((rgbaData[i * 4 + 1] / 255.0f) * factorG, 0.0f, 1.0f) * 255.0f);
            rgbaData[i * 4 + 2] = static_cast<uint8_t>(std::clamp((rgbaData[i * 4 + 2] / 255.0f) * factorB, 0.0f, 1.0f) * 255.0f);
            rgbaData[i * 4 + 3] = static_cast<uint8_t>(std::clamp((rgbaData[i * 4 + 3] / 255.0f) * factorA, 0.0f, 1.0f) * 255.0f);
        }
    };

    if (textureIndex < 0 || textureIndex >= static_cast<int>(model->textures.size()))
    {
        std::vector<uint8_t> rgbaData(4, 255);
        applyBaseColorFactor(rgbaData, 1);
        createTextureFromPixelData(rgbaData.data(), rgbaData.size(), 1, 1, VK_FORMAT_R8G8B8A8_SRGB);
        return true;
    }

    const auto& textureInfo = model->textures[textureIndex];
    if (textureInfo.source < 0 || textureInfo.source >= static_cast<int>(model->images.size()))
    {
        return false;
    }

    const auto& image = model->images[textureInfo.source];
    if (image.image.empty() || image.width <= 0 || image.height <= 0)
    {
        return false;
    }

    if (image.pixel_type != TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE)
    {
        std::cerr << "[Warning] Unsupported GLTF texture pixel type for primitive texture." << std::endl;
        return false;
    }

    std::vector<uint8_t> rgbaData;
    size_t pixelCount = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
    rgbaData.resize(pixelCount * 4);

    if (image.component == 4)
    {
        memcpy(rgbaData.data(), image.image.data(), rgbaData.size());
    }
    else if (image.component == 3)
    {
        for (size_t i = 0; i < pixelCount; ++i)
        {
            rgbaData[i * 4 + 0] = image.image[i * 3 + 0];
            rgbaData[i * 4 + 1] = image.image[i * 3 + 1];
            rgbaData[i * 4 + 2] = image.image[i * 3 + 2];
            rgbaData[i * 4 + 3] = 255;
        }
    }
    else
    {
        std::cerr << "[Warning] Unsupported GLTF texture component count: " << image.component << std::endl;
        return false;
    }

    applyBaseColorFactor(rgbaData, pixelCount);
    createTextureFromPixelData(rgbaData.data(), rgbaData.size(), image.width, image.height, VK_FORMAT_R8G8B8A8_SRGB);
    return true;
}

void Primitive::finalizeTextureResources()
{
    if (textureImage == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Texture image was not created before finalization.");
    }

    createTextureImageView();

    if (textureImageView == VK_NULL_HANDLE || textureSampler == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Failed to finalize texture resources!");
    }

    const VkResult allocResult = engine->allocateDescriptorSet(
        engine->descriptorPool,
        engine->primitiveDescriptorSetLayout,
        primitiveDescriptorSet);
    if (allocResult != VK_SUCCESS)
    {
        std::cerr << "[Primitive] Descriptor set allocation failed. result=" << allocResult
                  << " primitive=" << this
                  << " mesh=" << mesh
                  << " vertexCount=" << vertexCount
                  << " textureFormat=" << static_cast<int>(textureFormat)
                  << " usesYuv=" << (usesYuvTexture ? "yes" : "no")
                  << std::endl;
        throw std::runtime_error("Failed to allocate texture descriptor set!");
    }

    engine->nameVulkanObject((uint64_t)primitiveDescriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, "TextureDescriptorSet");

    updateDescriptorSet();
}
