#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

#include <QImage>
#include <QString>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

#include "object_transform.h"
#include <../tinygltf/tiny_gltf.h>

class Engine;
class Mesh;
struct Vertex;
struct SharedTextureResources;

enum class PrimitiveYuvFormat : uint32_t {
    None = 0,
    NV12 = 1,
    Planar420 = 2,
    Planar422 = 3,
    Planar444 = 4
};

enum class PrimitiveAlphaMode : uint32_t {
    Opaque = 0,
    Mask = 1,
    Blend = 2
};

enum class PrimitiveCullMode : uint32_t {
    Back = 0,
    Disabled = 1,
    Front = 2
};


class Primitive {
public:
    Primitive(Engine* engine, Mesh* mesh, const std::vector<Vertex>& vertices, bool initializeTextureResources = true);
    Primitive(Engine* engine, Mesh* mesh, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices, int materialIndex);
    Primitive(Engine* engine, Mesh* mesh, tinygltf::Primitive tprimitive);
    ~Primitive();

    void updateDescriptorSet();
    void updateUniformBuffer(const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj);
    void createTextureResources();
    void createTextureResources(const tinygltf::Model* model, const tinygltf::Primitive& tprimitive);
    void createDefaultTexture();
    void createTextureSampler();
    void createTextureImageView();
    void finalizeTextureResources();
    void createTextureFromPixelData(const void* pixelData,
                                    size_t dataSize,
                                    uint32_t width,
                                    uint32_t height,
                                    VkFormat format,
                                    uint32_t rowStrideBytes = 0);
    void updateTextureFromPixelData(const void* pixelData, size_t dataSize, uint32_t width, uint32_t height, VkFormat format, uint32_t rowStrideBytes = 0);
    void updateTextureFromNV12(const uint8_t* nv12Data, size_t dataSize, uint32_t width, uint32_t height);
    void updateTextureFromPlanarYuv(const uint8_t* yPlane,
                                    size_t yPlaneBytes,
                                    uint32_t width,
                                    uint32_t height,
                                    const uint8_t* uvPlane,
                                    size_t uvPlaneBytes,
                                    uint32_t chromaWidth,
                                    uint32_t chromaHeight,
                                    bool sixteenBit,
                                    PrimitiveYuvFormat formatTag);
    void updateChromaPlaneTexture(const void* pixelData, size_t dataSize, uint32_t width, uint32_t height, VkFormat format);
    void setYuvColorMetadata(uint32_t colorSpace, uint32_t colorRange);
    void enableTextureDoubleBuffering();
    bool createTextureFromGLTF(const tinygltf::Model* model, const tinygltf::Primitive& tprimitive);
    void createIndexBuffer(const std::vector<uint32_t>& indices);
    void updateVertexData(const std::vector<Vertex>& vertices);
    void updateSkinningMatrices(const std::vector<glm::mat4>& matrices);
    ObjectTransform buildObjectTransformData() const;
    void uploadInstanceTransforms();
    void markInstanceTransformsDirty();
    Mesh* mesh;

    // Vertex data
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    uint32_t vertexCount;
    VkBuffer indexBuffer;
    VkDeviceMemory indexBufferMemory;
    uint32_t indexCount;
    std::vector<Vertex> cpuVertices;

    // Transformation data
    glm::mat4 transform;
    glm::vec3 rotation;
    uint32_t instanceCount = 1;
    std::array<glm::vec3, kMaxPrimitiveInstances> instanceOffsets{};
    std::array<glm::vec4, kMaxPrimitiveInstances> instanceRotations{};
    
    // Texture resources
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    VkImage textureImageInactive = VK_NULL_HANDLE;
    VkDeviceMemory textureImageMemoryInactive = VK_NULL_HANDLE;
    VkImageView textureImageViewInactive = VK_NULL_HANDLE;
    VkSampler textureSampler;
    uint32_t textureWidth = 0;
    uint32_t textureHeight = 0;
    VkFormat textureFormat = VK_FORMAT_UNDEFINED;
    VkImage chromaImage = VK_NULL_HANDLE;
    VkDeviceMemory chromaImageMemory = VK_NULL_HANDLE;
    VkImageView chromaImageView = VK_NULL_HANDLE;
    VkImage chromaImageInactive = VK_NULL_HANDLE;
    VkDeviceMemory chromaImageMemoryInactive = VK_NULL_HANDLE;
    VkImageView chromaImageViewInactive = VK_NULL_HANDLE;
    uint32_t chromaWidth = 0;
    uint32_t chromaHeight = 0;
    VkFormat chromaFormat = VK_FORMAT_UNDEFINED;
    bool usesYuvTexture = false;
    PrimitiveYuvFormat yuvTextureFormat = PrimitiveYuvFormat::None;
    uint32_t yuvChromaDivX = 1;
    uint32_t yuvChromaDivY = 1;
    uint32_t yuvBitDepth = 8;
    uint32_t yuvColorSpace = 0;
    uint32_t yuvColorRange = 0;
    bool textureDoubleBuffered = false;
    bool textureInactiveInitialized = false;
    bool chromaInactiveInitialized = false;
    PrimitiveAlphaMode alphaMode = PrimitiveAlphaMode::Opaque;
    PrimitiveCullMode cullMode = PrimitiveCullMode::Back;
    float alphaCutoff = 0.5f;
    bool paintOverrideEnabled = false;
    bool forceAlphaOne = false;
    bool gpuSkinningEnabled = false;
    uint32_t skinJointCount = 0;
    glm::vec3 paintOverrideColor = glm::vec3(1.0f, 0.0f, 1.0f);
    std::shared_ptr<SharedTextureResources> sharedTextureResources;
    QImage texturePreviewImage;
    int sourceMaterialIndex = -1;
    QString sourceMaterialName;
    QString sourceTextureLabel;
    float sourceOpacityScalar = 1.0f;
    bool sourceHasOpacityTexture = false;
    bool sourceOpacityInverted = false;
    QString sourceOpacityTextureLabel;
    // various info
    VkSamplerCreateInfo samplerInfo{};
    VkDescriptorBufferInfo bufferInfo{};
    VkDescriptorImageInfo imageInfo{};
    VkImageViewCreateInfo viewInfo{};
    VkBufferCreateInfo stagingBufferInfo{};
    VkMemoryAllocateInfo stagingAllocInfo{};
    VkImageCreateInfo textureImageInfo{};
    VkImageMemoryBarrier imageBarrier{};
    VkBufferImageCopy imageCopyRegion{};
    VkImageMemoryBarrier shaderBarrier{};
    
    // Uniform buffer
    VkBuffer ObjectTransformUBO;
    VkDeviceMemory ObjectTransformUBOBufferMemory;
    void* ObjectTransformUBOMapped;
    VkBuffer skinningBuffer = VK_NULL_HANDLE;
    VkDeviceMemory skinningBufferMemory = VK_NULL_HANDLE;
    void* skinningBufferMapped = nullptr;
    VkBuffer instanceDataBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceDataBufferMemory = VK_NULL_HANDLE;
    void* instanceDataBufferMapped = nullptr;
    VkDeviceSize instanceDataBufferSize = 0;
    bool instanceTransformsDirty = true;
    
    // Descriptor set for rendering
    VkDescriptorSet primitiveDescriptorSet;
    
    Engine* engine;
};
