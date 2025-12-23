#include "overlay.hpp"
#include "detection.hpp"

#include <cstring>
#include <exception>
#include <iostream>
#include <vector>

#include <vulkan/vulkan.h>
#include <glm/glm.hpp>

#include "engine.h"
#include "glyph.h"
#include "utils.h"

namespace overlay
{

struct YOLOOverlayCompute
{
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    
    // Detection buffer descriptor
    VkDescriptorBufferInfo detectionBufferInfo{};
};

void destroyYOLOOverlayCompute(YOLOOverlayCompute& comp)
{
    if (comp.fence != VK_NULL_HANDLE)
    {
        vkDestroyFence(comp.device, comp.fence, nullptr);
        comp.fence = VK_NULL_HANDLE;
    }
    if (comp.commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(comp.device, comp.commandPool, nullptr);
        comp.commandPool = VK_NULL_HANDLE;
    }
    if (comp.descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(comp.device, comp.descriptorPool, nullptr);
        comp.descriptorPool = VK_NULL_HANDLE;
    }
    if (comp.pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(comp.device, comp.pipeline, nullptr);
        comp.pipeline = VK_NULL_HANDLE;
    }
    if (comp.pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(comp.device, comp.pipelineLayout, nullptr);
        comp.pipelineLayout = VK_NULL_HANDLE;
    }
    if (comp.descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(comp.device, comp.descriptorSetLayout, nullptr);
        comp.descriptorSetLayout = VK_NULL_HANDLE;
    }
}

bool initializeYOLOOverlayCompute(Engine* engine, YOLOOverlayCompute& comp)
{
    comp.device = engine->logicalDevice;
    comp.queue = engine->graphicsQueue;

    // Create descriptor set layout with two bindings:
    // 0: Storage image for output
    // 1: Storage buffer for detections
    VkDescriptorSetLayoutBinding bindings[2] = {};
    
    // Binding 0: Storage image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 1: Storage buffer for detections
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(comp.device, &layoutInfo, nullptr, &comp.descriptorSetLayout) != VK_SUCCESS)
    {
        std::cerr << "[YOLO Overlay] Failed to create descriptor set layout" << std::endl;
        return false;
    }

    // Load YOLO overlay shader
    std::vector<char> shaderCode;
    try
    {
        shaderCode = readSPIRVFile("shaders/overlay_rect_yolo.comp.spv");
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[YOLO Overlay] " << ex.what() << std::endl;
        // Try to compile the shader if it doesn't exist
        std::cerr << "[YOLO Overlay] Shader not compiled, using fallback" << std::endl;
        destroyYOLOOverlayCompute(comp);
        return false;
    }

    VkShaderModule shaderModule = engine->createShaderModule(shaderCode);

    // Push constant range for YOLO overlay
    struct YOLOPush {
        glm::vec2 outputSize;
        glm::vec2 rectCenter;
        glm::vec2 rectSize;
        float outerThickness;
        float innerThickness;
        float detectionEnabled;
        uint32_t detectionCount;
        uint32_t padding; // For alignment
    };
    
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(YOLOPush);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &comp.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(comp.device, &pipelineLayoutInfo, nullptr, &comp.pipelineLayout) != VK_SUCCESS)
    {
        std::cerr << "[YOLO Overlay] Failed to create pipeline layout" << std::endl;
        vkDestroyShaderModule(comp.device, shaderModule, nullptr);
        destroyYOLOOverlayCompute(comp);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = comp.pipelineLayout;

    if (vkCreateComputePipelines(comp.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &comp.pipeline) != VK_SUCCESS)
    {
        std::cerr << "[YOLO Overlay] Failed to create compute pipeline" << std::endl;
        vkDestroyShaderModule(comp.device, shaderModule, nullptr);
        destroyYOLOOverlayCompute(comp);
        return false;
    }

    vkDestroyShaderModule(comp.device, shaderModule, nullptr);

    // Create descriptor pool
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(comp.device, &poolInfo, nullptr, &comp.descriptorPool) != VK_SUCCESS)
    {
        std::cerr << "[YOLO Overlay] Failed to create descriptor pool" << std::endl;
        destroyYOLOOverlayCompute(comp);
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = comp.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &comp.descriptorSetLayout;

    if (vkAllocateDescriptorSets(comp.device, &allocInfo, &comp.descriptorSet) != VK_SUCCESS)
    {
        std::cerr << "[YOLO Overlay] Failed to allocate descriptor set" << std::endl;
        destroyYOLOOverlayCompute(comp);
        return false;
    }

    // Create command pool and buffer
    VkCommandPoolCreateInfo poolCreateInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCreateInfo.queueFamilyIndex = engine->graphicsQueueFamilyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(comp.device, &poolCreateInfo, nullptr, &comp.commandPool) != VK_SUCCESS)
    {
        std::cerr << "[YOLO Overlay] Failed to create command pool" << std::endl;
        destroyYOLOOverlayCompute(comp);
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = comp.commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(comp.device, &cmdAllocInfo, &comp.commandBuffer) != VK_SUCCESS)
    {
        std::cerr << "[YOLO Overlay] Failed to allocate command buffer" << std::endl;
        destroyYOLOOverlayCompute(comp);
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(comp.device, &fenceInfo, nullptr, &comp.fence) != VK_SUCCESS)
    {
        std::cerr << "[YOLO Overlay] Failed to create fence" << std::endl;
        destroyYOLOOverlayCompute(comp);
        return false;
    }

    return true;
}

void runYOLOOverlayCompute(Engine* engine,
                           YOLOOverlayCompute& comp,
                           ImageResource& target,
                           uint32_t width,
                           uint32_t height,
                           const glm::vec2& rectCenter,
                           const glm::vec2& rectSize,
                           float outerThickness,
                           float innerThickness,
                           float detectionEnabled,
                           const detection::DetectionBuffer& detectionBuffer)
{
    bool recreated = false;
    if (!ensureImageResource(engine, target, width, height, VK_FORMAT_R8G8B8A8_UNORM, recreated,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        std::cerr << "[YOLO Overlay] Failed to ensure image resource" << std::endl;
        return;
    }

    comp.width = width;
    comp.height = height;

    // Update descriptor set with storage image
    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageInfo.imageView = target.view;

    // Update descriptor set with detection buffer
    comp.detectionBufferInfo = detectionBuffer.descriptor;

    // Check if detection buffer is valid
    if (detectionBuffer.buffer == VK_NULL_HANDLE) {
        std::cerr << "[YOLO Overlay] Detection buffer is null, using fallback" << std::endl;
        return;
    }

    VkWriteDescriptorSet writes[2] = {};
    
    // Write 0: Storage image
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = comp.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &storageInfo;
    
    // Write 1: Storage buffer
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = comp.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &comp.detectionBufferInfo;
    
    vkUpdateDescriptorSets(comp.device, 2, writes, 0, nullptr);

    // Record command buffer
    vkResetCommandBuffer(comp.commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(comp.commandBuffer, &beginInfo);

    // Push constants
    struct YOLOPush {
        glm::vec2 outputSize;
        glm::vec2 rectCenter;
        glm::vec2 rectSize;
        float outerThickness;
        float innerThickness;
        float detectionEnabled;
        uint32_t detectionCount;
        uint32_t padding; // For alignment
    } push{
        glm::vec2(static_cast<float>(width), static_cast<float>(height)),
        rectCenter,
        rectSize,
        outerThickness,
        innerThickness,
        detectionEnabled,
        static_cast<uint32_t>(detectionBuffer.count),
        0 // padding
    };

    // Transition image to GENERAL layout
    const VkImageLayout initialLayout = recreated ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageMemoryBarrier toGeneralBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toGeneralBarrier.oldLayout = initialLayout;
    toGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.image = target.image;
    toGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toGeneralBarrier.subresourceRange.baseMipLevel = 0;
    toGeneralBarrier.subresourceRange.levelCount = 1;
    toGeneralBarrier.subresourceRange.baseArrayLayer = 0;
    toGeneralBarrier.subresourceRange.layerCount = 1;
    toGeneralBarrier.srcAccessMask = (initialLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                         ? VK_ACCESS_SHADER_READ_BIT
                                         : 0;
    toGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    VkPipelineStageFlags srcStage = (initialLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                        ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    vkCmdPipelineBarrier(comp.commandBuffer,
                         srcStage,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toGeneralBarrier);

    // Bind pipeline and descriptors
    vkCmdBindPipeline(comp.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, comp.pipeline);
    vkCmdBindDescriptorSets(comp.commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            comp.pipelineLayout,
                            0,
                            1,
                            &comp.descriptorSet,
                            0,
                            nullptr);
    
    // Push constants
    vkCmdPushConstants(comp.commandBuffer,
                       comp.pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(YOLOPush),
                       &push);

    // Dispatch compute
    const uint32_t groupX = (width + 15) / 16;
    const uint32_t groupY = (height + 15) / 16;
    vkCmdDispatch(comp.commandBuffer, groupX, groupY, 1);

    // Transition image back to SHADER_READ_ONLY_OPTIMAL
    VkImageMemoryBarrier toReadBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.image = target.image;
    toReadBarrier.subresourceRange = toGeneralBarrier.subresourceRange;
    toReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(comp.commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toReadBarrier);

    vkEndCommandBuffer(comp.commandBuffer);

    // Submit and wait
    vkResetFences(comp.device, 1, &comp.fence);
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &comp.commandBuffer;
    vkQueueSubmit(engine->graphicsQueue, 1, &submitInfo, comp.fence);
    vkWaitForFences(comp.device, 1, &comp.fence, VK_TRUE, UINT64_MAX);
}

struct PoseOverlayCompute
{
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    uint32_t width = 0;
    uint32_t height = 0;
    
    // Detection buffer descriptor
    VkDescriptorBufferInfo detectionBufferInfo{};
};

void destroyPoseOverlayCompute(PoseOverlayCompute& comp)
{
    if (comp.fence != VK_NULL_HANDLE)
    {
        vkDestroyFence(comp.device, comp.fence, nullptr);
        comp.fence = VK_NULL_HANDLE;
    }
    if (comp.commandPool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(comp.device, comp.commandPool, nullptr);
        comp.commandPool = VK_NULL_HANDLE;
    }
    if (comp.descriptorPool != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorPool(comp.device, comp.descriptorPool, nullptr);
        comp.descriptorPool = VK_NULL_HANDLE;
    }
    if (comp.pipeline != VK_NULL_HANDLE)
    {
        vkDestroyPipeline(comp.device, comp.pipeline, nullptr);
        comp.pipeline = VK_NULL_HANDLE;
    }
    if (comp.pipelineLayout != VK_NULL_HANDLE)
    {
        vkDestroyPipelineLayout(comp.device, comp.pipelineLayout, nullptr);
        comp.pipelineLayout = VK_NULL_HANDLE;
    }
    if (comp.descriptorSetLayout != VK_NULL_HANDLE)
    {
        vkDestroyDescriptorSetLayout(comp.device, comp.descriptorSetLayout, nullptr);
        comp.descriptorSetLayout = VK_NULL_HANDLE;
    }
}

bool initializePoseOverlayCompute(Engine* engine, PoseOverlayCompute& comp)
{
    comp.device = engine->logicalDevice;
    comp.queue = engine->graphicsQueue;

    // Create descriptor set layout with two bindings:
    // 0: Storage image for output
    // 1: Storage buffer for detections
    VkDescriptorSetLayoutBinding bindings[2] = {};
    
    // Binding 0: Storage image
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    
    // Binding 1: Storage buffer for detections
    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    if (vkCreateDescriptorSetLayout(comp.device, &layoutInfo, nullptr, &comp.descriptorSetLayout) != VK_SUCCESS)
    {
        std::cerr << "[Pose Overlay] Failed to create descriptor set layout" << std::endl;
        return false;
    }

    // Load pose overlay shader
    std::vector<char> shaderCode;
    try
    {
        shaderCode = readSPIRVFile("shaders/overlay_pose.comp.spv");
    }
    catch (const std::exception& ex)
    {
        std::cerr << "[Pose Overlay] " << ex.what() << std::endl;
        // Try to compile the shader if it doesn't exist
        std::cerr << "[Pose Overlay] Shader not compiled, using fallback" << std::endl;
        destroyPoseOverlayCompute(comp);
        return false;
    }

    VkShaderModule shaderModule = engine->createShaderModule(shaderCode);

    // Push constant range for pose overlay
    struct PosePush {
        glm::vec2 outputSize;
        glm::vec2 rectCenter;
        glm::vec2 rectSize;
        float outerThickness;
        float innerThickness;
        float detectionEnabled;
        uint32_t detectionCount;
        uint32_t padding; // For alignment
    };
    
    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = sizeof(PosePush);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &comp.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;
    pipelineLayoutInfo.pPushConstantRanges = &pushRange;

    if (vkCreatePipelineLayout(comp.device, &pipelineLayoutInfo, nullptr, &comp.pipelineLayout) != VK_SUCCESS)
    {
        std::cerr << "[Pose Overlay] Failed to create pipeline layout" << std::endl;
        vkDestroyShaderModule(comp.device, shaderModule, nullptr);
        destroyPoseOverlayCompute(comp);
        return false;
    }

    VkPipelineShaderStageCreateInfo stageInfo{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stageInfo.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = comp.pipelineLayout;

    if (vkCreateComputePipelines(comp.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &comp.pipeline) != VK_SUCCESS)
    {
        std::cerr << "[Pose Overlay] Failed to create compute pipeline" << std::endl;
        vkDestroyShaderModule(comp.device, shaderModule, nullptr);
        destroyPoseOverlayCompute(comp);
        return false;
    }

    vkDestroyShaderModule(comp.device, shaderModule, nullptr);

    // Create descriptor pool
    VkDescriptorPoolSize poolSizes[2] = {};
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    poolSizes[0].descriptorCount = 1;
    poolSizes[1].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[1].descriptorCount = 1;

    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.maxSets = 1;
    poolInfo.poolSizeCount = 2;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(comp.device, &poolInfo, nullptr, &comp.descriptorPool) != VK_SUCCESS)
    {
        std::cerr << "[Pose Overlay] Failed to create descriptor pool" << std::endl;
        destroyPoseOverlayCompute(comp);
        return false;
    }

    // Allocate descriptor set
    VkDescriptorSetAllocateInfo allocInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocInfo.descriptorPool = comp.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &comp.descriptorSetLayout;

    if (vkAllocateDescriptorSets(comp.device, &allocInfo, &comp.descriptorSet) != VK_SUCCESS)
    {
        std::cerr << "[Pose Overlay] Failed to allocate descriptor set" << std::endl;
        destroyPoseOverlayCompute(comp);
        return false;
    }

    // Create command pool and buffer
    VkCommandPoolCreateInfo poolCreateInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolCreateInfo.queueFamilyIndex = engine->graphicsQueueFamilyIndex;
    poolCreateInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    if (vkCreateCommandPool(comp.device, &poolCreateInfo, nullptr, &comp.commandPool) != VK_SUCCESS)
    {
        std::cerr << "[Pose Overlay] Failed to create command pool" << std::endl;
        destroyPoseOverlayCompute(comp);
        return false;
    }

    VkCommandBufferAllocateInfo cmdAllocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cmdAllocInfo.commandPool = comp.commandPool;
    cmdAllocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdAllocInfo.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(comp.device, &cmdAllocInfo, &comp.commandBuffer) != VK_SUCCESS)
    {
        std::cerr << "[Pose Overlay] Failed to allocate command buffer" << std::endl;
        destroyPoseOverlayCompute(comp);
        return false;
    }

    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(comp.device, &fenceInfo, nullptr, &comp.fence) != VK_SUCCESS)
    {
        std::cerr << "[Pose Overlay] Failed to create fence" << std::endl;
        destroyPoseOverlayCompute(comp);
        return false;
    }

    return true;
}

void runPoseOverlayCompute(Engine* engine,
                           PoseOverlayCompute& comp,
                           ImageResource& target,
                           uint32_t width,
                           uint32_t height,
                           const glm::vec2& rectCenter,
                           const glm::vec2& rectSize,
                           float outerThickness,
                           float innerThickness,
                           float detectionEnabled,
                           const detection::DetectionBuffer& detectionBuffer)
{
    bool recreated = false;
    if (!ensureImageResource(engine, target, width, height, VK_FORMAT_R8G8B8A8_UNORM, recreated,
                             VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT))
    {
        std::cerr << "[Pose Overlay] Failed to ensure image resource" << std::endl;
        return;
    }

    comp.width = width;
    comp.height = height;

    // Update descriptor set with storage image
    VkDescriptorImageInfo storageInfo{};
    storageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    storageInfo.imageView = target.view;

    // Update descriptor set with detection buffer
    comp.detectionBufferInfo = detectionBuffer.descriptor;

    // Check if detection buffer is valid
    if (detectionBuffer.buffer == VK_NULL_HANDLE) {
        std::cerr << "[Pose Overlay] Detection buffer is null, using fallback" << std::endl;
        return;
    }

    VkWriteDescriptorSet writes[2] = {};
    
    // Write 0: Storage image
    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = comp.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[0].descriptorCount = 1;
    writes[0].pImageInfo = &storageInfo;
    
    // Write 1: Storage buffer
    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = comp.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &comp.detectionBufferInfo;
    
    vkUpdateDescriptorSets(comp.device, 2, writes, 0, nullptr);

    // Record command buffer
    vkResetCommandBuffer(comp.commandBuffer, 0);
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(comp.commandBuffer, &beginInfo);

    // Push constants
    struct PosePush {
        glm::vec2 outputSize;
        glm::vec2 rectCenter;
        glm::vec2 rectSize;
        float outerThickness;
        float innerThickness;
        float detectionEnabled;
        uint32_t detectionCount;
        uint32_t padding; // For alignment
    } push{
        glm::vec2(static_cast<float>(width), static_cast<float>(height)),
        rectCenter,
        rectSize,
        outerThickness,
        innerThickness,
        detectionEnabled,
        static_cast<uint32_t>(detectionBuffer.count),
        0 // padding
    };

    // Transition image to GENERAL layout
    const VkImageLayout initialLayout = recreated ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkImageMemoryBarrier toGeneralBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toGeneralBarrier.oldLayout = initialLayout;
    toGeneralBarrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    toGeneralBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toGeneralBarrier.image = target.image;
    toGeneralBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toGeneralBarrier.subresourceRange.baseMipLevel = 0;
    toGeneralBarrier.subresourceRange.levelCount = 1;
    toGeneralBarrier.subresourceRange.baseArrayLayer = 0;
    toGeneralBarrier.subresourceRange.layerCount = 1;
    toGeneralBarrier.srcAccessMask = (initialLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                         ? VK_ACCESS_SHADER_READ_BIT
                                         : 0;
    toGeneralBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;

    VkPipelineStageFlags srcStage = (initialLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
                                        ? VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT
                                        : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;

    vkCmdPipelineBarrier(comp.commandBuffer,
                         srcStage,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toGeneralBarrier);

    // Bind pipeline and descriptors
    vkCmdBindPipeline(comp.commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, comp.pipeline);
    vkCmdBindDescriptorSets(comp.commandBuffer,
                            VK_PIPELINE_BIND_POINT_COMPUTE,
                            comp.pipelineLayout,
                            0,
                            1,
                            &comp.descriptorSet,
                            0,
                            nullptr);
    
    // Push constants
    vkCmdPushConstants(comp.commandBuffer,
                       comp.pipelineLayout,
                       VK_SHADER_STAGE_COMPUTE_BIT,
                       0,
                       sizeof(PosePush),
                       &push);

    // Dispatch compute
    const uint32_t groupX = (width + 15) / 16;
    const uint32_t groupY = (height + 15) / 16;
    vkCmdDispatch(comp.commandBuffer, groupX, groupY, 1);

    // Transition image back to SHADER_READ_ONLY_OPTIMAL
    VkImageMemoryBarrier toReadBarrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toReadBarrier.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    toReadBarrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    toReadBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toReadBarrier.image = target.image;
    toReadBarrier.subresourceRange = toGeneralBarrier.subresourceRange;
    toReadBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    toReadBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

    vkCmdPipelineBarrier(comp.commandBuffer,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         0,
                         0, nullptr,
                         0, nullptr,
                         1, &toReadBarrier);

    vkEndCommandBuffer(comp.commandBuffer);

    // Submit and wait
    vkResetFences(comp.device, 1, &comp.fence);
    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &comp.commandBuffer;
    vkQueueSubmit(engine->graphicsQueue, 1, &submitInfo, comp.fence);
    vkWaitForFences(comp.device, 1, &comp.fence, VK_TRUE, UINT64_MAX);
}

} // namespace overlay
