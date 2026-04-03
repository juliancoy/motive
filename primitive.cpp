#include "primitive.h"

#include "engine.h"
#include "model.h"

#include <cstring>
#include <numeric>
#include <stdexcept>

namespace
{

struct InstanceGpuData
{
    glm::vec4 offset = glm::vec4(0.0f);
    glm::vec4 rotation = glm::vec4(0.0f);
};

glm::mat3 normalMatrixFromTransform(const glm::mat4& transform)
{
    return glm::transpose(glm::inverse(glm::mat3(transform)));
}

bool extractVerticesAndIndicesFromGltfPrimitiveLocal(const tinygltf::Model& model,
                                                     const tinygltf::Primitive& primitive,
                                                     const glm::mat4& transform,
                                                     std::vector<Vertex>& outVertices,
                                                     std::vector<uint32_t>& outIndices)
{
    if (primitive.attributes.count("POSITION") == 0 ||
        primitive.attributes.count("NORMAL") == 0 ||
        primitive.attributes.count("TEXCOORD_0") == 0)
    {
        return false;
    }

    const auto& posAccessor = model.accessors[primitive.attributes.at("POSITION")];
    const auto& normAccessor = model.accessors[primitive.attributes.at("NORMAL")];
    const auto& texAccessor = model.accessors[primitive.attributes.at("TEXCOORD_0")];

    const auto& posView = model.bufferViews[posAccessor.bufferView];
    const auto& normView = model.bufferViews[normAccessor.bufferView];
    const auto& texView = model.bufferViews[texAccessor.bufferView];

    const float* positions = reinterpret_cast<const float*>(&model.buffers[posView.buffer].data[posView.byteOffset + posAccessor.byteOffset]);
    const float* normals = reinterpret_cast<const float*>(&model.buffers[normView.buffer].data[normView.byteOffset + normAccessor.byteOffset]);
    const float* texCoords = reinterpret_cast<const float*>(&model.buffers[texView.buffer].data[texView.byteOffset + texAccessor.byteOffset]);

    if (posAccessor.count != normAccessor.count || posAccessor.count != texAccessor.count)
    {
        throw std::runtime_error("GLTF attribute counts mismatch.");
    }

    outVertices.resize(posAccessor.count);
    const glm::mat3 normalTransform = normalMatrixFromTransform(transform);
    for (size_t i = 0; i < posAccessor.count; ++i)
    {
        Vertex vertex{};
        const glm::vec3 localPosition(positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2]);
        const glm::vec3 localNormal(normals[i * 3], normals[i * 3 + 1], normals[i * 3 + 2]);
        vertex.pos = glm::vec3(transform * glm::vec4(localPosition, 1.0f));
        vertex.normal = glm::normalize(normalTransform * localNormal);
        vertex.texCoord = glm::vec2(texCoords[i * 2], texCoords[i * 2 + 1]);
        outVertices[i] = vertex;
    }

    outIndices.clear();
    if (primitive.indices >= 0)
    {
        const auto& idxAccessor = model.accessors[primitive.indices];
        const auto& idxView = model.bufferViews[idxAccessor.bufferView];
        const uint8_t* data = &model.buffers[idxView.buffer].data[idxView.byteOffset + idxAccessor.byteOffset];
        outIndices.reserve(idxAccessor.count);

        switch (idxAccessor.componentType)
        {
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
        {
            const uint16_t* indices = reinterpret_cast<const uint16_t*>(data);
            for (size_t i = 0; i < idxAccessor.count; ++i) outIndices.push_back(indices[i]);
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
        {
            const uint32_t* indices = reinterpret_cast<const uint32_t*>(data);
            for (size_t i = 0; i < idxAccessor.count; ++i) outIndices.push_back(indices[i]);
            break;
        }
        case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
        {
            const uint8_t* indices = reinterpret_cast<const uint8_t*>(data);
            for (size_t i = 0; i < idxAccessor.count; ++i) outIndices.push_back(indices[i]);
            break;
        }
        default:
            throw std::runtime_error("Unsupported GLTF index component type.");
        }
    }
    else
    {
        outIndices.resize(outVertices.size());
        std::iota(outIndices.begin(), outIndices.end(), 0);
    }

    return true;
}

} // namespace

Primitive::Primitive(Engine *engine, Mesh *mesh, const std::vector<Vertex> &vertices, bool initializeTextureResources)
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
      textureImageInactive(VK_NULL_HANDLE),
      textureImageMemoryInactive(VK_NULL_HANDLE),
      textureImageViewInactive(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      ObjectTransformUBO(VK_NULL_HANDLE),
      ObjectTransformUBOBufferMemory(VK_NULL_HANDLE),
      ObjectTransformUBOMapped(nullptr),
      primitiveDescriptorSet(VK_NULL_HANDLE)
{
    instanceOffsets.fill(glm::vec3(0.0f));
    instanceRotations.fill(glm::vec4(0.0f));

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

    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);

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

    const VkDeviceSize skinningBufferSize = sizeof(SkinMatrixPalette);
    engine->createBuffer(
        skinningBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        skinningBuffer,
        skinningBufferMemory);
    vkMapMemory(engine->logicalDevice, skinningBufferMemory, 0, skinningBufferSize, 0, &skinningBufferMapped);
    SkinMatrixPalette initialSkinPalette{};
    for (glm::mat4& joint : initialSkinPalette.joints)
    {
        joint = glm::mat4(1.0f);
    }
    std::memcpy(skinningBufferMapped, &initialSkinPalette, sizeof(initialSkinPalette));

    instanceDataBufferSize = sizeof(InstanceGpuData) * kMaxPrimitiveInstances;
    engine->createBuffer(
        instanceDataBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceDataBuffer,
        instanceDataBufferMemory);
    vkMapMemory(engine->logicalDevice, instanceDataBufferMemory, 0, instanceDataBufferSize, 0, &instanceDataBufferMapped);
    std::memset(instanceDataBufferMapped, 0, static_cast<size_t>(instanceDataBufferSize));

    if (initializeTextureResources)
    {
        createTextureResources();
    }
}

Primitive::Primitive(Engine *engine,
                     Mesh *mesh,
                     const std::vector<Vertex> &vertices,
                     const std::vector<uint32_t> &indices,
                     int materialIndex)
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
      textureImageInactive(VK_NULL_HANDLE),
      textureImageMemoryInactive(VK_NULL_HANDLE),
      textureImageViewInactive(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      ObjectTransformUBO(VK_NULL_HANDLE),
      ObjectTransformUBOBufferMemory(VK_NULL_HANDLE),
      ObjectTransformUBOMapped(nullptr),
      primitiveDescriptorSet(VK_NULL_HANDLE)
{
    instanceOffsets.fill(glm::vec3(0.0f));
    instanceRotations.fill(glm::vec4(0.0f));

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
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
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

    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);
    createIndexBuffer(indices);

    VkDeviceSize uboSize = sizeof(ObjectTransform);
    engine->createBuffer(
        uboSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        ObjectTransformUBO,
        ObjectTransformUBOBufferMemory);

    vkMapMemory(engine->logicalDevice, ObjectTransformUBOBufferMemory, 0, uboSize, 0, &ObjectTransformUBOMapped);

    const VkDeviceSize skinningBufferSize = sizeof(SkinMatrixPalette);
    engine->createBuffer(
        skinningBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        skinningBuffer,
        skinningBufferMemory);
    vkMapMemory(engine->logicalDevice, skinningBufferMemory, 0, skinningBufferSize, 0, &skinningBufferMapped);
    SkinMatrixPalette initialSkinPalette{};
    for (glm::mat4& joint : initialSkinPalette.joints)
    {
        joint = glm::mat4(1.0f);
    }
    std::memcpy(skinningBufferMapped, &initialSkinPalette, sizeof(initialSkinPalette));

    instanceDataBufferSize = sizeof(InstanceGpuData) * kMaxPrimitiveInstances;
    engine->createBuffer(
        instanceDataBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceDataBuffer,
        instanceDataBufferMemory);
    vkMapMemory(engine->logicalDevice, instanceDataBufferMemory, 0, instanceDataBufferSize, 0, &instanceDataBufferMapped);
    std::memset(instanceDataBufferMapped, 0, static_cast<size_t>(instanceDataBufferSize));

    tinygltf::Primitive materialPrimitive;
    materialPrimitive.material = materialIndex;
    createTextureResources(mesh->model->tgltfModel, materialPrimitive);
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
      textureImageInactive(VK_NULL_HANDLE),
      textureImageMemoryInactive(VK_NULL_HANDLE),
      textureImageViewInactive(VK_NULL_HANDLE),
      textureSampler(VK_NULL_HANDLE),
      ObjectTransformUBO(VK_NULL_HANDLE),
      ObjectTransformUBOBufferMemory(VK_NULL_HANDLE),
      ObjectTransformUBOMapped(nullptr),
      primitiveDescriptorSet(VK_NULL_HANDLE)
{
    instanceOffsets.fill(glm::vec3(0.0f));
    instanceRotations.fill(glm::vec4(0.0f));
    Model *model = mesh->model;
    tinygltf::Model *tgltfModel = model->tgltfModel;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    if (!extractVerticesAndIndicesFromGltfPrimitiveLocal(*tgltfModel, tprimitive, glm::mat4(1.0f), vertices, indices))
    {
        return;
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

    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);

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

    const VkDeviceSize skinningBufferSize = sizeof(SkinMatrixPalette);
    engine->createBuffer(
        skinningBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        skinningBuffer,
        skinningBufferMemory);
    vkMapMemory(engine->logicalDevice, skinningBufferMemory, 0, skinningBufferSize, 0, &skinningBufferMapped);
    SkinMatrixPalette initialSkinPalette{};
    for (glm::mat4& joint : initialSkinPalette.joints)
    {
        joint = glm::mat4(1.0f);
    }
    std::memcpy(skinningBufferMapped, &initialSkinPalette, sizeof(initialSkinPalette));

    instanceDataBufferSize = sizeof(InstanceGpuData) * kMaxPrimitiveInstances;
    engine->createBuffer(
        instanceDataBufferSize,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        instanceDataBuffer,
        instanceDataBufferMemory);
    vkMapMemory(engine->logicalDevice, instanceDataBufferMemory, 0, instanceDataBufferSize, 0, &instanceDataBufferMapped);
    std::memset(instanceDataBufferMapped, 0, static_cast<size_t>(instanceDataBufferSize));

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

    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);
}

void Primitive::updateVertexData(const std::vector<Vertex>& vertices)
{
    if (vertices.size() != cpuVertices.size())
    {
        throw std::runtime_error("Animated vertex update changed vertex count.");
    }

    cpuVertices = vertices;
    const VkDeviceSize bufferSize = sizeof(Vertex) * vertices.size();

    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VkDeviceMemory stagingBufferMemory = VK_NULL_HANDLE;
    engine->createBuffer(
        bufferSize,
        VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        stagingBuffer,
        stagingBufferMemory);

    void* data = nullptr;
    vkMapMemory(engine->logicalDevice, stagingBufferMemory, 0, bufferSize, 0, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vkUnmapMemory(engine->logicalDevice, stagingBufferMemory);

    VkCommandBuffer cmdBuffer = engine->beginSingleTimeCommands();
    VkBufferCopy copyRegion{};
    copyRegion.size = bufferSize;
    vkCmdCopyBuffer(cmdBuffer, stagingBuffer, vertexBuffer, 1, &copyRegion);
    engine->endSingleTimeCommands(cmdBuffer);
    engine->deferStagingBufferDestruction(stagingBuffer, stagingBufferMemory);
}

void Primitive::updateSkinningMatrices(const std::vector<glm::mat4>& matrices)
{
    if (!skinningBufferMapped)
    {
        return;
    }

    SkinMatrixPalette palette{};
    for (glm::mat4& joint : palette.joints)
    {
        joint = glm::mat4(1.0f);
    }

    const size_t count = std::min(matrices.size(), palette.joints.size());
    for (size_t i = 0; i < count; ++i)
    {
        palette.joints[i] = matrices[i];
    }

    skinJointCount = static_cast<uint32_t>(count);
    gpuSkinningEnabled = skinJointCount > 0;
    std::memcpy(skinningBufferMapped, &palette, sizeof(palette));
}

Primitive::~Primitive()
{
    const bool ownsSharedTexture = !sharedTextureResources;
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
    if (skinningBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, skinningBuffer, nullptr);
    }
    if (skinningBufferMemory != VK_NULL_HANDLE)
    {
        if (skinningBufferMapped)
        {
            vkUnmapMemory(engine->logicalDevice, skinningBufferMemory);
        }
        vkFreeMemory(engine->logicalDevice, skinningBufferMemory, nullptr);
    }
    if (instanceDataBuffer != VK_NULL_HANDLE)
    {
        vkDestroyBuffer(engine->logicalDevice, instanceDataBuffer, nullptr);
    }
    if (instanceDataBufferMemory != VK_NULL_HANDLE)
    {
        if (instanceDataBufferMapped)
        {
            vkUnmapMemory(engine->logicalDevice, instanceDataBufferMemory);
        }
        vkFreeMemory(engine->logicalDevice, instanceDataBufferMemory, nullptr);
    }

    // Free descriptor set if allocated
    if (primitiveDescriptorSet != VK_NULL_HANDLE)
    {
        engine->freeDescriptorSet(engine->descriptorPool, primitiveDescriptorSet);
    }

    // Destroy texture resources
    if (ownsSharedTexture && textureImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, textureImageView, nullptr);
    }
    if (ownsSharedTexture && textureImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, textureImage, nullptr);
    }
    if (ownsSharedTexture && textureImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, textureImageMemory, nullptr);
    }
    if (textureImageViewInactive != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, textureImageViewInactive, nullptr);
    }
    if (textureImageInactive != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, textureImageInactive, nullptr);
    }
    if (textureImageMemoryInactive != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, textureImageMemoryInactive, nullptr);
    }

    if (chromaImageView != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, chromaImageView, nullptr);
    }
    if (chromaImage != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, chromaImage, nullptr);
    }
    if (chromaImageMemory != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, chromaImageMemory, nullptr);
    }
    if (chromaImageViewInactive != VK_NULL_HANDLE)
    {
        vkDestroyImageView(engine->logicalDevice, chromaImageViewInactive, nullptr);
    }
    if (chromaImageInactive != VK_NULL_HANDLE)
    {
        vkDestroyImage(engine->logicalDevice, chromaImageInactive, nullptr);
    }
    if (chromaImageMemoryInactive != VK_NULL_HANDLE)
    {
        vkFreeMemory(engine->logicalDevice, chromaImageMemoryInactive, nullptr);
    }

    // Destroy sampler
    if (ownsSharedTexture && textureSampler != VK_NULL_HANDLE)
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
    uploadInstanceTransforms();
    ObjectTransform thisTransformUBO = buildObjectTransformData();
    thisTransformUBO.model = model * transform;
    memcpy(ObjectTransformUBOMapped, &thisTransformUBO, sizeof(thisTransformUBO));
}

ObjectTransform Primitive::buildObjectTransformData() const
{
    ObjectTransform data{};
    const uint32_t clampedCount = std::max(1u, std::min(instanceCount, kMaxPrimitiveInstances));
    data.model = transform;
    const uint32_t formatValue = usesYuvTexture ? static_cast<uint32_t>(yuvTextureFormat) : 0u;
    data.instanceData = glm::uvec4(clampedCount,
                                   formatValue,
                                   yuvColorSpace,
                                   yuvColorRange);
    data.yuvParams = glm::uvec4(usesYuvTexture ? yuvChromaDivX : 1u,
                                usesYuvTexture ? yuvChromaDivY : 1u,
                                usesYuvTexture ? yuvBitDepth : 8u,
                                0u);
    data.materialFlags = glm::uvec4(static_cast<uint32_t>(alphaMode),
                                    paintOverrideEnabled ? 1u : 0u,
                                    forceAlphaOne ? 1u : 0u,
                                    0u);
    data.materialParams = glm::vec4(alphaCutoff, 0.0f, 0.0f, 0.0f);
    data.colorOverride = glm::vec4(paintOverrideColor, 1.0f);
    return data;
}

void Primitive::markInstanceTransformsDirty()
{
    instanceTransformsDirty = true;
}

void Primitive::uploadInstanceTransforms()
{
    if (!instanceTransformsDirty || !instanceDataBufferMapped)
    {
        return;
    }

    auto *gpuData = reinterpret_cast<InstanceGpuData *>(instanceDataBufferMapped);
    for (uint32_t i = 0; i < kMaxPrimitiveInstances; ++i)
    {
        const bool active = i < instanceCount;
        const glm::vec3 offset = active ? instanceOffsets[i] : glm::vec3(0.0f);
        const glm::vec4 rotationVec = active ? instanceRotations[i] : glm::vec4(0.0f);
        gpuData[i].offset = glm::vec4(offset, 0.0f);
        gpuData[i].rotation = rotationVec;
    }
    instanceTransformsDirty = false;
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

    VkDescriptorImageInfo chromaInfo = imageInfo;
    if (chromaImageView != VK_NULL_HANDLE)
    {
        chromaInfo.imageView = chromaImageView;
    }

    // Fill descriptor writes for UBO and sampler
    std::array<VkWriteDescriptorSet, 5> descriptorWrites{};

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

    // Binding 2: Chroma sampler (falls back to base texture when unused)
    descriptorWrites[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[2].dstSet = primitiveDescriptorSet;
    descriptorWrites[2].dstBinding = 2;
    descriptorWrites[2].dstArrayElement = 0;
    descriptorWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrites[2].descriptorCount = 1;
    descriptorWrites[2].pImageInfo = &chromaInfo;

    VkDescriptorBufferInfo instanceTransformBufferInfo{};
    instanceTransformBufferInfo.buffer = instanceDataBuffer;
    instanceTransformBufferInfo.offset = 0;
    instanceTransformBufferInfo.range = instanceDataBufferSize;

    descriptorWrites[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[3].dstSet = primitiveDescriptorSet;
    descriptorWrites[3].dstBinding = 3;
    descriptorWrites[3].dstArrayElement = 0;
    descriptorWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[3].descriptorCount = 1;
    descriptorWrites[3].pBufferInfo = &instanceTransformBufferInfo;

    VkDescriptorBufferInfo skinningBufferInfo{};
    skinningBufferInfo.buffer = skinningBuffer;
    skinningBufferInfo.offset = 0;
    skinningBufferInfo.range = sizeof(SkinMatrixPalette);

    descriptorWrites[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    descriptorWrites[4].dstSet = primitiveDescriptorSet;
    descriptorWrites[4].dstBinding = 4;
    descriptorWrites[4].dstArrayElement = 0;
    descriptorWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    descriptorWrites[4].descriptorCount = 1;
    descriptorWrites[4].pBufferInfo = &skinningBufferInfo;

    vkUpdateDescriptorSets(engine->logicalDevice,
                           static_cast<uint32_t>(descriptorWrites.size()),
                           descriptorWrites.data(),
                           0, nullptr);
}

void Primitive::setYuvColorMetadata(uint32_t colorSpace, uint32_t colorRange)
{
    yuvColorSpace = colorSpace;
    yuvColorRange = colorRange;
}

void Primitive::enableTextureDoubleBuffering()
{
    textureDoubleBuffered = true;
}
