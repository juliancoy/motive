#include "model.h"
#include "engine.h"
#include <glm/glm.hpp>
#include <memory>
#include <numeric>
#include <utility>
#include <limits>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/component_wise.hpp>
#include <iostream>

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
      indexBuffer(VK_NULL_HANDLE),
      indexBufferMemory(VK_NULL_HANDLE),
      indexCount(0),
      transform(glm::mat4(1.0f)),
      rotation(glm::vec3(0.0f)),
      textureImage(VK_NULL_HANDLE),
      textureImageMemory(VK_NULL_HANDLE),
      textureImageView(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      ObjectTransformUBO(VK_NULL_HANDLE),
      ObjectTransformUBOBufferMemory(VK_NULL_HANDLE),
      ObjectTransformUBOMapped(nullptr),
      primitiveDescriptorSet(VK_NULL_HANDLE)
{
    // Create vertex buffer
    cpuVertices = vertices;
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

    std::vector<uint32_t> sequentialIndices(vertexCount);
    std::iota(sequentialIndices.begin(), sequentialIndices.end(), 0);
    createIndexBuffer(sequentialIndices);

    // Create uniform buffer
    VkDeviceSize ObjectTransformUBOSize = sizeof(ObjectTransform);
    engine->createBuffer(
        ObjectTransformUBOSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ObjectTransformUBO,
        ObjectTransformUBOBufferMemory);

    vkMapMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory, 0, ObjectTransformUBOSize, 0, &ObjectTransformUBOMapped);

    createTextureResources();
}

Primitive::Primitive(Engine *engine, Mesh *mesh, tinygltf::Primitive tprimitive)
    : engine(engine),
      mesh(mesh),
      vertexBuffer(VK_NULL_HANDLE),
      vertexBufferMemory(VK_NULL_HANDLE),
      vertexCount(0),
      indexBuffer(VK_NULL_HANDLE),
      indexBufferMemory(VK_NULL_HANDLE),
      indexCount(0),
      transform(glm::mat4(1.0f)),
      rotation(glm::vec3(0.0f)),
      textureImage(VK_NULL_HANDLE),
      textureImageMemory(VK_NULL_HANDLE),
      textureImageView(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      ObjectTransformUBO(VK_NULL_HANDLE),
      ObjectTransformUBOBufferMemory(VK_NULL_HANDLE),
      ObjectTransformUBOMapped(nullptr),
      primitiveDescriptorSet(VK_NULL_HANDLE)
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
    Model *model = mesh->model;
    tinygltf::Model *tgltfModel = model->tgltfModel;
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

    cpuVertices = vertices;
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

    std::vector<uint32_t> indices;
    if (tprimitive.indices >= 0)
    {
        const auto &indexAccessor = tgltfModel->accessors[tprimitive.indices];
        const auto &indexView = tgltfModel->bufferViews[indexAccessor.bufferView];
        const auto &indexBufferData = tgltfModel->buffers[indexView.buffer];

        const uint8_t *dataPtr = reinterpret_cast<const uint8_t *>(
            &indexBufferData.data[indexView.byteOffset + indexAccessor.byteOffset]);

        auto componentSize = [](int componentType) -> size_t {
            switch (componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                return sizeof(uint8_t);
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                return sizeof(uint16_t);
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                return sizeof(uint32_t);
            default:
                throw std::runtime_error("Unsupported index component type in GLTF");
            }
        };

        size_t stride = indexView.byteStride ? indexView.byteStride : componentSize(indexAccessor.componentType);
        indices.resize(indexAccessor.count);

        for (size_t i = 0; i < indexAccessor.count; ++i)
        {
            const uint8_t *elementPtr = dataPtr + i * stride;
            switch (indexAccessor.componentType)
            {
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
                indices[i] = *reinterpret_cast<const uint8_t *>(elementPtr);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
                indices[i] = *reinterpret_cast<const uint16_t *>(elementPtr);
                break;
            case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
                indices[i] = *reinterpret_cast<const uint32_t *>(elementPtr);
                break;
            default:
                throw std::runtime_error("Unsupported index component type in GLTF");
            }
        }
    }
    else
    {
        indices.resize(vertexCount);
        std::iota(indices.begin(), indices.end(), 0);
    }
    createIndexBuffer(indices);

    // Create uniform buffer
    VkDeviceSize uboSize = sizeof(ObjectTransform);
    engine->createBuffer(
        uboSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ObjectTransformUBO,
        ObjectTransformUBOBufferMemory);

    vkMapMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory, 0, uboSize, 0, &ObjectTransformUBOMapped);

    // Create texture resources using GLTF data when available
    createTextureResources(tgltfModel, tprimitive);
}

void Primitive::createIndexBuffer(const std::vector<uint32_t> &indices)
{
    if (indices.empty())
    {
        indexCount = 0;
        indexBuffer = VK_NULL_HANDLE;
        indexBufferMemory = VK_NULL_HANDLE;
        return;
    }

    indexCount = static_cast<uint32_t>(indices.size());
    VkDeviceSize bufferSize = sizeof(uint32_t) * indices.size();

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    void *data = nullptr;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        indexBuffer,
        indexBufferMemory);

    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, indexBuffer, 1, &copyRegion);
    engine->endSingleTimeCommands(cmdBuffer);

    vkDestroyBuffer(engine->logicalDevice, stagingBuffer, nullptr);
    vkFreeMemory(engine->logicalDevice, stagingBufferMemory, nullptr);
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
    if (ObjectTransformUBO != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, ObjectTransformUBO, nullptr);
    }
    if (ObjectTransformUBOBufferMemory != VK_NULL_HANDLE)
    {
        if (ObjectTransformUBOMapped)
        {
            vkUnmapMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory);
        }
        vkFreeMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory, nullptr);
    }

    // Free descriptor set if allocated
    if (primitiveDescriptorSet != VK_NULL_HANDLE)
    {
        vkFreeDescriptorSets(engine->logicalDevice, engine->descriptorPool, 1, &primitiveDescriptorSet);
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
    if (indexBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, indexBuffer, nullptr);
    }
    if (indexBufferMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, indexBufferMemory, nullptr);
    }
}

void Primitive::updateUniformBuffer(const glm::mat4 &model, const glm::mat4 &view, const glm::mat4 &proj)
{
    ObjectTransform thisTransformUBO{};
    thisTransformUBO.model = model * transform;
    memcpy(ObjectTransformUBOMapped, &thisTransformUBO, sizeof(thisTransformUBO));
}

void Primitive::updateDescriptorSet()
{
    if (primitiveDescriptorSet == VK_NULL_HANDLE)
    {
        throw std::runtime_error("Cannot update descriptor set: descriptor set is null.");
    }

    // Setup descriptor buffer info for UBO (binding 0)
    VkDescriptorBufferInfo thisObjectTransformBufferInfo{};
    thisObjectTransformBufferInfo.buffer = ObjectTransformUBO;
    thisObjectTransformBufferInfo.offset = 0;
    thisObjectTransformBufferInfo.range = sizeof(ObjectTransform);

    // Setup descriptor image info for texture sampler (binding 1)
    VkDescriptorImageInfo imageInfo{};
    imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.imageView = textureImageView;
    imageInfo.sampler = textureSampler;

    // Fill descriptor writes for UBO and sampler
    std::array<VkWriteDescriptorSet, 2> descriptorWrites{};

    // Binding 0: Uniform Buffer Object
    descriptorWrites[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[0].dstSet = primitiveDescriptorSet;
    descriptorWrites[0].dstBinding = 0;
    descriptorWrites[0].dstArrayElement = 0;
    descriptorWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[0].descriptorCount = 1;
    descriptorWrites[0].pBufferInfo = &thisObjectTransformBufferInfo;

    // Binding 1: Combined Image Sampler
    descriptorWrites[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[1].dstSet = primitiveDescriptorSet;
    descriptorWrites[1].dstBinding = 1;
    descriptorWrites[1].dstArrayElement = 0;
    descriptorWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[1].descriptorCount = 1;
    descriptorWrites[1].pImageInfo = &imageInfo;

    vkUpdateDescriptorSets(engine->logicalDevice,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(),
                           0, nullptr);
}


Mesh::Mesh(Engine *engine, Model *model, tinygltf::Mesh gltfmesh)
    : engine(engine), model(model)
{
    for (const auto &tprimitive : gltfmesh.primitives)
    {
        primitives.emplace_back(std::make_unique<Primitive>(engine, this, tprimitive));
    }
}

Mesh::Mesh(Engine *engine, Model *model, const std::vector<Vertex> &vertices)
    : engine(engine), model(model)
{
    primitives.emplace_back(std::make_unique<Primitive>(engine, this, vertices));
}

Mesh::Mesh(Mesh &&other) noexcept
    : model(other.model),
      primitives(std::move(other.primitives)),
      engine(other.engine)
{
}

Mesh &Mesh::operator=(Mesh &&other) noexcept
{
    if (this != &other)
    {
        model = other.model;
        engine = other.engine;
        primitives = std::move(other.primitives);
    }
    return *this;
}

Mesh::~Mesh()
{
}

Model::Model(const std::vector<Vertex> &vertices, Engine *engine) : engine(engine)
{
    std::cout << "[Debug] Creating procedural Model at " << this << " with " << vertices.size() << " vertices." << std::endl;
    // Create a single mesh from the provided vertices
    meshes.emplace_back(engine, this, vertices);
    std::cout << "[Debug] Model " << this << " finished initialization. Mesh count: " << meshes.size() << std::endl;
}

Model::Model(const std::string &gltfPath, Engine *engine) : engine(engine)
{
    name = gltfPath;
    std::cout << "[Debug] Loading GLTF Model at " << this << " from " << gltfPath << std::endl;
    tinygltf::TinyGLTF loader;
    std::string err, warn;

    bool success = false;
    const std::string ext = gltfPath.substr(gltfPath.find_last_of('.'));
    // initialize the model
    tgltfModel = new tinygltf::Model();

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
        meshes.emplace_back(engine, this, gltfmesh);
    }
    std::cout << "[Debug] GLTF Model " << this << " loaded successfully. Mesh count: " << meshes.size() << std::endl;
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

    // Clean up GLTF model storage if it was allocated
    if (tgltfModel != nullptr)
    {
        delete tgltfModel;
        tgltfModel = nullptr;
    }
}

void Model::scaleToUnitBox()
{
    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());
    bool foundPositions = false;

    if (tgltfModel)
    {
        for (const auto &meshData : tgltfModel->meshes)
        {
            for (const auto &primitiveData : meshData.primitives)
            {
                auto attrIt = primitiveData.attributes.find("POSITION");
                if (attrIt == primitiveData.attributes.end())
                {
                    continue;
                }

                const tinygltf::Accessor &accessor = tgltfModel->accessors[attrIt->second];
                const tinygltf::BufferView &bufferView = tgltfModel->bufferViews[accessor.bufferView];
                const tinygltf::Buffer &buffer = tgltfModel->buffers[bufferView.buffer];

                size_t stride = accessor.ByteStride(bufferView);
                if (stride == 0)
                {
                    stride = 3 * sizeof(float);
                }

                const uint8_t *dataPtr = buffer.data.data() + bufferView.byteOffset + accessor.byteOffset;
                for (size_t i = 0; i < accessor.count; ++i)
                {
                    const float *position = reinterpret_cast<const float *>(dataPtr + i * stride);
                    glm::vec3 pos(position[0], position[1], position[2]);
                    minBounds = glm::min(minBounds, pos);
                    maxBounds = glm::max(maxBounds, pos);
                    foundPositions = true;
                }
            }
        }
    }
    else
    {
        foundPositions = computeProceduralBounds(minBounds, maxBounds);
        if (!foundPositions)
        {
            std::cerr << "[Warning] scaleToUnitBox: Unable to compute bounds for procedural model " << name << std::endl;
            return;
        }
    }

    if (!foundPositions)
    {
        std::cerr << "[Warning] scaleToUnitBox: No position data found on model " << name << std::endl;
        return;
    }

    glm::vec3 extent = maxBounds - minBounds;
    float maxExtent = glm::compMax(extent);
    if (maxExtent <= 0.0f || !std::isfinite(maxExtent))
    {
        std::cerr << "[Warning] scaleToUnitBox: Invalid extent for model " << name << std::endl;
        return;
    }

    glm::vec3 center = (minBounds + maxBounds) * 0.5f;
    float scale = 1.0f / maxExtent;

    glm::mat4 translation = glm::translate(glm::mat4(1.0f), -center);
    glm::mat4 scaleMat = glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    glm::mat4 transform = scaleMat * translation;

    for (auto &mesh : meshes)
    {
        for (auto &primitive : mesh.primitives)
        {
            if (primitive)
            {
                primitive->transform = transform;
                if (primitive->ObjectTransformUBOMapped)
                {
                    ObjectTransform updated{};
                    updated.model = primitive->transform;
                    memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
                }
            }
        }
    }

    std::cout << "[Debug] Model " << name << " scaled to unit box (scale=" << scale << ")" << std::endl;
}

void Model::resizeToUnitBox()
{
    // Currently identical to scaleToUnitBox but exposed with the requested name
    scaleToUnitBox();
}

void Model::translate(const glm::vec3 &offset)
{
    applyTransformToPrimitives(glm::translate(glm::mat4(1.0f), offset));
}

void Model::rotate(float angleRadians, const glm::vec3 &axis)
{
    glm::vec3 normAxis = glm::length(axis) == 0.0f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::normalize(axis);
    applyTransformToPrimitives(glm::rotate(glm::mat4(1.0f), angleRadians, normAxis));
}

void Model::rotate(float xDegrees, float yDegrees, float zDegrees)
{
    glm::mat4 rotationMat(1.0f);

    if (xDegrees != 0.0f)
    {
        rotationMat = glm::rotate(rotationMat, glm::radians(xDegrees), glm::vec3(1.0f, 0.0f, 0.0f));
    }
    if (yDegrees != 0.0f)
    {
        rotationMat = glm::rotate(rotationMat, glm::radians(yDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
    }
    if (zDegrees != 0.0f)
    {
        rotationMat = glm::rotate(rotationMat, glm::radians(zDegrees), glm::vec3(0.0f, 0.0f, 1.0f));
    }

    applyTransformToPrimitives(rotationMat);
}

void Model::applyTransformToPrimitives(const glm::mat4 &transform)
{
    for (auto &mesh : meshes)
    {
        for (auto &primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }

            primitive->transform = transform * primitive->transform;

            if (primitive->ObjectTransformUBOMapped)
            {
                ObjectTransform updated{};
                updated.model = primitive->transform;
                memcpy(primitive->ObjectTransformUBOMapped, &updated, sizeof(updated));
            }
        }
    }
}

bool Model::computeProceduralBounds(glm::vec3 &minBounds, glm::vec3 &maxBounds) const
{
    bool found = false;
    for (const auto &mesh : meshes)
    {
        for (const auto &primitive : mesh.primitives)
        {
            if (!primitive)
            {
                continue;
            }

            for (const auto &vertex : primitive->cpuVertices)
            {
                minBounds = glm::min(minBounds, vertex.pos);
                maxBounds = glm::max(maxBounds, vertex.pos);
                found = true;
            }
        }
    }
    return found;
}
