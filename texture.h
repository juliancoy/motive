#pragma once

#include <vector>
#include <string>
#include <GLFW/glfw3.h>
#include <../tinygltf/tiny_gltf.h>
#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include "model.h"

class Engine;  // Forward declaration
class Model;  // Forward declaration
class Mesh;  // Forward declaration
class Texture;  // Forward declaration

class Material{
public:
    Material(Engine* engine, Model* model, Primitive* primitive, tinygltf::Material tmaterial);
    ~Material();
    std::vector<Texture*> textures;
};


class Texture {
public:
    Texture(Engine* engine, Mesh* mesh, const std::vector<Vertex>& vertices);
    Texture(Engine* engine, Mesh* mesh, tinygltf::Texture tTexture);
    ~Texture();

    void updateDescriptorSet();
    void updateUniformBuffer(const glm::mat4& model, const glm::mat4& view, const glm::mat4& proj);
    void createTextureResources();
    void createDefaultTexture();
    void createTextureSampler();
    void createTextureImageView();
    Mesh* mesh;

    // Texture resources
    VkImage textureImage;
    VkDeviceMemory textureImageMemory;
    VkImageView textureImageView;
    VkSampler textureSampler;
    VkDescriptorSetLayoutCreateInfo textureLayoutInfo{};
    VkDescriptorSetLayoutBinding samplerLayoutBinding{};

    // various info
    VkSamplerCreateInfo samplerInfo{};
    VkDescriptorBufferInfo bufferInfo{};
    VkDescriptorImageInfo imageInfo{};
    VkImageViewCreateInfo viewInfo{};
    VkDescriptorSetAllocateInfo allocInfo{};
    VkBufferCreateInfo stagingBufferInfo{};
    VkMemoryAllocateInfo stagingAllocInfo{};
    VkImageCreateInfo textureImageInfo{};
    VkImageMemoryBarrier imageBarrier{};
    VkBufferImageCopy imageCopyRegion{};
    
    Engine* engine;
};
