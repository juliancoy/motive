#pragma once

#include <vector>
#include <string>
#include <GLFW/glfw3.h>
#include <../tinygltf/tiny_gltf.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>

class Engine;  // Forward declaration

struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    static VkVertexInputBindingDescription getBindingDescription();
    static std::array<VkVertexInputAttributeDescription, 3> getAttributeDescriptions();
};

class Mesh {
public:
    Mesh(Engine* engine, const std::vector<Vertex>& vertices);
    ~Mesh();
    void updateDescriptorSet(VkDescriptorSet descriptorSet);
    void updateUniformBuffer(const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj);
    void draw(VkCommandBuffer commandBuffer);
    void createTextureSampler();
    void createTextureImageView();
    void createDefaultTexture();

    VkBuffer vertexBuffer;
    VkDeviceMemory vertexBufferMemory;
    uint32_t vertexCount;
    glm::mat4 transform;
    glm::vec3 rotation;
    
    VkImage gltfTextureImage;
    VkDeviceMemory gltfTextureImageMemory;
    VkImageView gltfTextureImageView;
    VkSampler textureSampler;
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    
    Engine* engine;
    VkBuffer uniformBuffer;
    VkDeviceMemory uniformBufferMemory;
    void* uniformBufferMapped;
};

class Model
{
public:
    Model(const std::string &gltfPath, Engine* engine);
    Model(const std::vector<Vertex> &vertices, Engine* engine);

    std::string name = "Added Vertices";
    std::vector<Mesh> meshes;


    VkImageCreateInfo imgCreateInfo{};
    Engine* engine;
    GLFWwindow* window;


private:
    void loadModelFromGltf(const std::string& gltfPath);
    void createFromVertices(const std::vector<Vertex>& vertices);
};