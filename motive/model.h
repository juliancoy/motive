#pragma once

#include <array>
#include <vector>
#include <string>
#include <GLFW/glfw3.h>
#include <memory>
#include <../tinygltf/tiny_gltf.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "object_transform.h"

class Engine;  // Forward declaration
class Model;  // Forward declaration
class Mesh;  // Forward declaration
class Texture;  // Forward declaration

enum class PrimitiveYuvFormat : uint32_t {
    None = 0,
    NV12 = 1,
    Planar420 = 2,
    Planar422 = 3,
    Planar444 = 4
};

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
};

class Primitive {
public:
    Primitive(Engine* engine, Mesh* mesh, const std::vector<Vertex>& vertices);
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
    void createTextureFromPixelData(const void* pixelData, size_t dataSize, uint32_t width, uint32_t height, VkFormat format);
    void updateTextureFromPixelData(const void* pixelData, size_t dataSize, uint32_t width, uint32_t height, VkFormat format);
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
    VkBuffer instanceDataBuffer = VK_NULL_HANDLE;
    VkDeviceMemory instanceDataBufferMemory = VK_NULL_HANDLE;
    void* instanceDataBufferMapped = nullptr;
    VkDeviceSize instanceDataBufferSize = 0;
    bool instanceTransformsDirty = true;
    
    // Descriptor set for rendering
    VkDescriptorSet primitiveDescriptorSet;
    
    Engine* engine;
};

class Mesh {
public:
    Mesh(Engine* engine, Model* model, const std::vector<Vertex>& vertices);
    Mesh(Engine* engine, Model* model, tinygltf::Mesh);
    Mesh(const Mesh&) = delete;
    Mesh& operator=(const Mesh&) = delete;
    Mesh(Mesh&& other) noexcept;
    Mesh& operator=(Mesh&& other) noexcept;
    ~Mesh();
    Model* model;

    std::vector<std::unique_ptr<Primitive>> primitives; 
    Engine* engine;
};

class Model {
public:
    Model(const std::string& gltfPath, Engine* engine);
    Model(const std::vector<Vertex>& vertices, Engine* engine);
    ~Model();
    void scaleToUnitBox();
    void resizeToUnitBox();
    void translate(const glm::vec3& offset);
    void rotate(float angleRadians, const glm::vec3& axis);
    void rotate(float xDegrees, float yDegrees, float zDegrees);

    tinygltf::Model* tgltfModel = nullptr;

    std::string name = "Added Vertices";
    std::vector<Mesh> meshes;
    Engine* engine;
    GLFWwindow* window;
    std::vector<Texture*> textures;
private:
    void applyTransformToPrimitives(const glm::mat4& transform);
    bool computeProceduralBounds(glm::vec3& minBounds, glm::vec3& maxBounds) const;
};
