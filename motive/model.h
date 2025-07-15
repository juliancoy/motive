#pragma once

#include <vector>
#include <string>
#include <GLFW/glfw3.h>
#include <../tinygltf/tiny_gltf.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

class Engine;  // Forward declaration
class Model;  // Forward declaration
class Mesh;  // Forward declaration

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
    void createDefaultTexture();
    void createTextureSampler();
    void createTextureImageView();
    Mesh* mesh;

    // Vertex data
    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    uint32_t vertexCount;

    // Transformation data
    glm::mat4 transform;
    glm::vec3 rotation;
    
    // Texture resources
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    VkSampler textureSampler;
    
    // Uniform buffer
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    void* uniformBufferMapped;
    
    // Descriptor set for rendering
    VkDescriptorSet descriptorSet;
    
    Engine* engine;
};

class Mesh {
public:
    Mesh(Engine* engine, Model* model, const std::vector<Vertex>& vertices);
    Mesh(Engine* engine, Model* model, tinygltf::Mesh);
    ~Mesh();
    Model* model;

    std::vector<Primitive> primitives;
    Engine* engine;
};

class Model {
public:
    Model(const std::string& gltfPath, Engine* engine);
    Model(const std::vector<Vertex>& vertices, Engine* engine);
    ~Model();

    tinygltf::Model* tgltfModel;

    std::string name = "Added Vertices";
    std::vector<Mesh> meshes;
    Engine* engine;
    GLFWwindow* window;
};