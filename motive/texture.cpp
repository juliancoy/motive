
#include "texture.h"
#include "engine.h"
#include "model.h"

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
    createTextureSampler();
    bool loaded = createTextureFromGLTF(model, tprimitive);
    if (!loaded)
    {
        createDefaultTexture();
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

void Primitive::createTextureFromPixelData(const void* pixelData, size_t dataSize, uint32_t width, uint32_t height, VkFormat format)
{
    if (!pixelData || dataSize == 0 || width == 0 || height == 0)
    {
        throw std::runtime_error("Invalid pixel data for texture creation.");
    }

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
    imageCopyRegion.bufferOffset = 0;
    imageCopyRegion.bufferRowLength = 0;
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

    textureWidth = width;
    textureHeight = height;
    textureFormat = format;

    // Cleanup staging resources
    vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(engine->logicalDevice, stagingBufferMemory, nullptr);
}

void Primitive::updateTextureFromPixelData(const void* pixelData, size_t dataSize, uint32_t width, uint32_t height, VkFormat format)
{
    if (!pixelData || dataSize == 0 || width == 0 || height == 0)
    {
        throw std::runtime_error("Invalid pixel data for texture update.");
    }

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
        createTextureFromPixelData(pixelData, dataSize, width, height, format);
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

    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();

    VkImageMemoryBarrier beginBarrier{};
    beginBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    beginBarrier.oldLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    beginBarrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    beginBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    beginBarrier.image = textureImage;
    beginBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    beginBarrier.subresourceRange.baseMipLevel = 0;
    beginBarrier.subresourceRange.levelCount = 1;
    beginBarrier.subresourceRange.baseArrayLayer = 0;
    beginBarrier.subresourceRange.layerCount = 1;
    beginBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    beginBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &beginBarrier);

    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
    region.bufferImageHeight = 0;
    region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    region.imageSubresource.mipLevel = 0;
    region.imageSubresource.baseArrayLayer = 0;
    region.imageSubresource.layerCount = 1;
    region.imageOffset = {0, 0, 0};
    region.imageExtent = {textureWidth, textureHeight, 1};

    vkCmdCopyBufferToImage(
        cmdBuffer,
        stagingBuffer,
        textureImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    VkImageMemoryBarrier endBarrier{};
    endBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    endBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    endBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    endBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    endBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    endBarrier.image = textureImage;
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

    engine->endSingleTimeCommands(cmdBuffer);

    vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(engine->logicalDevice, stagingMemory, nullptr);
}

bool Primitive::createTextureFromGLTF(const tinygltf::Model* model, const tinygltf::Primitive& tprimitive)
{
    if (!model || tprimitive.material < 0 || tprimitive.material >= static_cast<int>(model->materials.size()))
    {
        return false;
    }

    const auto& material = model->materials[tprimitive.material];
    int textureIndex = material.pbrMetallicRoughness.baseColorTexture.index;
    if (textureIndex < 0 || textureIndex >= static_cast<int>(model->textures.size()))
    {
        return false;
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

    VkDescriptorSetAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = engine->descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &engine->primitiveDescriptorSetLayout;

    if (vkAllocateDescriptorSets(engine->logicalDevice, &allocInfo, &primitiveDescriptorSet) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate texture descriptor set!");
    }

    engine->nameVulkanObject((uint64_t)primitiveDescriptorSet, VK_OBJECT_TYPE_DESCRIPTOR_SET, "TextureDescriptorSet");

    updateDescriptorSet();
}
