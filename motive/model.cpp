#include "model.h"
#include "engine.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>

VkVertexInputBindingDescription Vertex::getBindingDescription()
{
    VkVertexInputBindingDescription bindingDescription{};
    bindingDescription.binding = 0;
    bindingDescription.stride = sizeof(Vertex);
    bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
    return bindingDescription;
}

std::array<VkVertexInputAttributeDescription, 3> Vertex::getAttributeDescriptions()
{
    std::array<VkVertexInputAttributeDescription, 3> attributeDescriptions{};

    attributeDescriptions[0].binding = 0;
    attributeDescriptions[0].location = 0;
    attributeDescriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[0].offset = offsetof(Vertex, pos);

    attributeDescriptions[1].binding = 0;
    attributeDescriptions[1].location = 1;
    attributeDescriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
    attributeDescriptions[1].offset = offsetof(Vertex, normal);

    attributeDescriptions[2].binding = 0;
    attributeDescriptions[2].location = 2;
    attributeDescriptions[2].format = VK_FORMAT_R32G32_SFLOAT;
    attributeDescriptions[2].offset = offsetof(Vertex, texCoord);

    return attributeDescriptions;
}

Primitive::Primitive(Engine *engine, Mesh *mesh, const std::vector<Vertex> &vertices)
    : engine(engine),
      mesh(mesh),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferMemory(VK_NULL_HANDLE),
      vertexCount(static_cast<uint32_t>(vertices.size())),
      transform(glm::mat4(1.0f)),
      rotation(glm::vec3(0.0f)),
      textureImage(VK_NULL_HANDLE),
      textureImageMemory(VK_NULL_HANDLE),
      textureImageView(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      uniformBuffer(VK_NULL_HANDLE),
      uniformBufferMemory(VK_NULL_HANDLE),
      uniformBufferMapped(nullptr),
      descriptorSet(VK_NULL_HANDLE)
{
    // Create vertex buffer
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    void *data;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBuffer,
        vertexBufferMemory);

    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, vertexBuffer, 1, &copyRegion);
    engine->endSingleTimeCommands(cmdBuffer);

    vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(engine->logicalDevice, stagingBufferMemory, nullptr);

    // Create uniform buffer
    VkDeviceSize uboSize = sizeof(UniformBufferObject);
    engine->createBuffer(
        uboSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        uniformBuffer,
        uniformBufferMemory);

    vkMapMemory(engine->logicalDevice, uniformBufferMemory, 0, uboSize, 0, &uniformBufferMapped);

    createTextureResources();
}

Primitive::Primitive(Engine* engine, Mesh* mesh, tinygltf::Primitive tprimitive)
    : engine(engine),
      mesh(mesh),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferMemory(VK_NULL_HANDLE),
      vertexCount(0),
      transform(glm::mat4(1.0f)),
      rotation(glm::vec3(0.0f)),
      textureImage(VK_NULL_HANDLE),
      textureImageMemory(VK_NULL_HANDLE),
      textureImageView(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      uniformBuffer(VK_NULL_HANDLE),
      uniformBufferMemory(VK_NULL_HANDLE),
      uniformBufferMapped(nullptr),
      descriptorSet(VK_NULL_HANDLE)
{
    // Initialize variables from tinygltf primitive
    if (tprimitive.attributes.count("POSITION") == 0 ||
        tprimitive.attributes.count("NORMAL") == 0 ||
        tprimitive.attributes.count("TEXCOORD_0") == 0)
    {
        std::cerr << "Skipping primitive due to missing attributes.\n";
        return;
    }

    // Load POSITION, NORMAL, TEXCOORD_0
    tinygltf::Model* tgltfModel = mesh->model->tgltfModel;
    const auto &posAccessor = tgltfModel->accessors[tprimitive.attributes.at("POSITION")];
    const auto &normAccessor = tgltfModel->accessors[tprimitive.attributes.at("NORMAL")];
    const auto &texAccessor = tgltfModel->accessors[tprimitive.attributes.at("TEXCOORD_0")];

    const auto &posView = tgltfModel->bufferViews[posAccessor.bufferView];
    const auto &normView = tgltfModel->bufferViews[normAccessor.bufferView];
    const auto &texView = tgltfModel->bufferViews[texAccessor.bufferView];

    const float *positions = reinterpret_cast<const float *>(&tgltfModel->buffers[posView.buffer].data[posView.byteOffset + posAccessor.byteOffset]);
    const float *normals = reinterpret_cast<const float *>(&tgltfModel->buffers[normView.buffer].data[normView.byteOffset + normAccessor.byteOffset]);
    const float *texCoords = reinterpret_cast<const float *>(&tgltfModel->buffers[texView.buffer].data[texView.byteOffset + texAccessor.byteOffset]);

    if (posAccessor.count != normAccessor.count || posAccessor.count != texAccessor.count)
        throw std::runtime_error("GLTF attribute counts mismatch.");

    std::vector<Vertex> vertices(posAccessor.count);
    for (size_t i = 0; i < posAccessor.count; ++i)
    {
        vertices[i].pos = glm::vec3(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
        vertices[i].normal = glm::vec3(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
        vertices[i].texCoord = glm::vec2(texCoords[i * 2], texCoords[i * 2 + 1]);
    }

    vertexCount = static_cast<uint32_t>(vertices.size());

    // Create vertex buffer
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();

    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    void *data;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), (size_t)bufferSize);
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        vertexBuffer,
        vertexBufferMemory);

    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, vertexBuffer, 1, &copyRegion);
    engine->endSingleTimeCommands(cmdBuffer);

    vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(engine->logicalDevice, stagingBufferMemory, nullptr);

    // Create uniform buffer
    VkDeviceSize uboSize = sizeof(UniformBufferObject);
    engine->createBuffer(
        uboSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        uniformBuffer,
        uniformBufferMemory);

    vkMapMemory(engine->logicalDevice, uniformBufferMemory, 0, uboSize, 0, &uniformBufferMapped);

    // Create texture resources
    createTextureSampler();
    createDefaultTexture();
    createTextureImageView();
    updateDescriptorSet();
}

Primitive::~Primitive()
{

    if (vertexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, vertexBuffer, nullptr);
    }
    if (vertexBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, vertexBufferMemory, nullptr);
    }
    if (uniformBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, uniformBuffer, nullptr);
    }
    if (uniformBufferMemory != VK_NULL_HANDLE)
    {
        if (uniformBufferMapped)
        {
            vkUnmapMemory(engine->logicalDevice, uniformBufferMemory);
        }
        vkFreeMemory(engine->logicalDevice, uniformBufferMemory, nullptr);
    }

    // Free descriptor set if allocated
    if (descriptorSet != VK_NULL_HANDLE)
    {
        vkFreeDescriptorSets(engine->logicalDevice, engine->descriptorPool, 1, &descriptorSet);
    }

    // Destroy texture resources
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

    // Destroy sampler
    if (textureSampler != VK_NULL_HANDLE)
    {
        vkDestroySampler(engine->logicalDevice, textureSampler, nullptr);
    }
}

void Primitive::updateDescriptorSet()
{
    if (descriptorSet == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Cannot update null descriptor set");
    }

    // Update UBO descriptor
    VkDescriptorBufferInfo bufferInfo{};
    bufferInfo.buffer = uniformBuffer;
    bufferInfo.offset = 0;
    bufferInfo.range = sizeof(UniformBufferObject);

    // Update texture descriptor if available
    VkDescriptorImageInfo imageInfo{};
    if (textureImageView != VK_NULL_HANDLE && textureSampler != VK_NULL_HANDLE)
    {
        imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        imageInfo.imageView = textureImageView;
        imageInfo.sampler = textureSampler;
    }

    std::array<VkWriteDescriptorSet, 2> descriptorWrites = {};

    // UBO at binding 0
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = descriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &bufferInfo;

    // Sampler at binding 1 (only if we have a texture)
    if (textureImageView != VK_NULL_HANDLE && textureSampler != VK_NULL_HANDLE)
    {
        descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        descriptorWrites[1].dstSet = descriptorSet;
        descriptorWrites[1].dstBinding = 1;
        descriptorWrites[1].dstArrayElement = 0;
        descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        descriptorWrites[1].descriptorCount = 1;
        descriptorWrites[1].pImageInfo = &imageInfo;
    }

    // Only update the texture descriptor if we have one
    uint32_t writeCount = (textureImageView != VK_NULL_HANDLE && textureSampler != VK_NULL_HANDLE) ? 2 : 1;
    vkUpdateDescriptorSets(engine->logicalDevice, writeCount, descriptorWrites.data(), 0, nullptr);
}

void Primitive::updateUniformBuffer(const glm::mat4 &model, const glm::mat4 &view, const glm::mat4 &proj)
{
    UniformBufferObject ubo{};
    ubo.model = model * transform;
    ubo.view = view;
    ubo.proj = proj;
    memcpy(uniformBufferMapped, &ubo, sizeof(ubo));
}

void Primitive::createTextureSampler()
{
    VkSamplerCreateInfo samplerInfo{};
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
    createTextureImageView();
    updateDescriptorSet();
}

void Primitive::createTextureImageView()
{
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = textureImage;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
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
    // Create a 1x1 white texture
    const uint32_t width = 1;
    const uint32_t height = 1;
    const uint32_t pixel = 0xFFFFFFFF; // White RGBA

    // Create staging buffer
    VkBuffer stagingBuffer;
    VkDeviceMemory stagingBufferMemory;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = sizeof(pixel);
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    if (vkCreateBuffer(engine->logicalDevice, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create staging buffer for default texture!");
    }

    // Allocate staging buffer memory
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(engine->logicalDevice, stagingBuffer, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &stagingBufferMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate staging buffer memory!");
    }

    vkBindBufferMemory(engine->logicalDevice, stagingBuffer, stagingBufferMemory, 0);

    // Copy texture data to staging buffer
    void *data;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, sizeof(pixel), 0, &data);
    memcpy(data, &pixel, sizeof(pixel));
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    // Create texture image
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = width;
    imageInfo.extent.height = height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = VK_FORMAT_R8G8B8A8_SRGB;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

    if (vkCreateImage(engine->logicalDevice, &imageInfo, nullptr, &textureImage) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create default texture image!");
    }

    // Allocate texture image memory
    vkGetImageMemoryRequirements(engine->logicalDevice, textureImage, &memRequirements);

    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex = engine->findMemoryType(memRequirements.memoryTypeBits,
                                                       VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(engine->logicalDevice, &allocInfo, nullptr, &textureImageMemory) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to allocate texture image memory!");
    }

    vkBindImageMemory(engine->logicalDevice, textureImage, textureImageMemory, 0);

    // Transition image layout for copying
    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();

    VkImageMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = textureImage;
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresourceRange.baseMipLevel = 0;
    barrier.subresourceRange.levelCount = 1;
    barrier.subresourceRange.baseArrayLayer = 0;
    barrier.subresourceRange.layerCount = 1;
    barrier.srcAccessMask = 0;
    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

    vkCmdPipelineBarrier(
        cmdBuffer,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,
        0, nullptr,
        0, nullptr,
        1, &barrier);

    // Copy buffer to image
    VkBufferImageCopy region{};
    region.bufferOffset = 0;
    region.bufferRowLength = 0;
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
        textureImage,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region);

    // Transition image layout to shader read
    VkImageMemoryBarrier shaderBarrier{};
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

    // Cleanup staging resources
    vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(engine->logicalDevice, stagingBufferMemory, nullptr);

    // Update descriptor set with sampler
    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = textureImageView;
    descImageInfo.sampler = textureSampler;

    VkWriteDescriptorSet descriptorWrite{};
    descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrite.dstSet = engine->descriptorSet;
    descriptorWrite.dstBinding = 1;
    descriptorWrite.dstArrayElement = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;

    vkUpdateDescriptorSets(engine->logicalDevice, 1, &descriptorWrite, 0, nullptr);
}

Mesh::Mesh(Engine *engine, Model *model, tinygltf::Mesh gltfmesh)
    : engine(engine)
{
    // Process all primitives in the mesh
    for (const auto &tprimitive : gltfmesh.primitives)
    {
        // Let the Primitive class handle the loading
        primitives.emplace_back(Primitive(engine, this, tprimitive));
    }
}

Mesh::Mesh(Engine *engine, Model *model, const std::vector<Vertex> &vertices)
    : engine(engine)
{
}

Mesh::~Mesh()
{
}

Model::Model(const std::vector<Vertex> &vertices, Engine *engine) : engine(engine)
{
    // Create a single mesh from the provided vertices
    meshes.emplace_back(Mesh(engine, this, vertices));
}

Model::Model(const std::string &gltfPath, Engine *engine) : engine(engine)
{
    name = gltfPath;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    tgltfModel = new tinygltf::Model();
    bool success = false;
    const std::string ext = gltfPath.substr(gltfPath.find_last_of('.'));

    if (ext == ".glb")
    {
        success = loader.LoadBinaryFromFile(tgltfModel, &err, &warn, gltfPath);
    }
    else if (ext == ".gltf")
    {
        success = loader.LoadASCIIFromFile(tgltfModel, &err, &warn, gltfPath);
    }
    else
    {
        throw std::runtime_error("Unsupported file extension: " + ext);
    }

    if (!warn.empty())
        std::cout << "GLTF warning: " << warn << std::endl;
    if (!err.empty())
        throw std::runtime_error("GLTF error: " + err);
    if (!success)
        throw std::runtime_error("Failed to load GLTF file: " + gltfPath);
    if (tgltfModel->meshes.empty())
        throw std::runtime_error("GLTF contains no meshes.");

    // Process all meshes in the GLTF file
    for (const auto &gltfmesh : tgltfModel->meshes)
    {
        // Create a new Mesh for each GLTF mesh
        meshes.emplace_back(Mesh(engine, this, gltfmesh));
    }
}

Model::~Model()
{
    // Wait for device to be idle before cleanup
    if (engine && engine->logicalDevice != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(engine->logicalDevice);
    }

    // Clear meshes vector - each Mesh's destructor will clean up its resources
    meshes.clear();

    // Clean up GLTF model
    if (tgltfModel) {
        delete tgltfModel;
        tgltfModel = nullptr;
    }
}
