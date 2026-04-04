#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <array>
#include <stdexcept>
#include <string>
#include <vector>

#include "camera.h"
#include "display.h"
#include "engine.h"
#include "model.h"
#include "utils.h"

namespace
{
uint32_t pipelineIndexForCullMode(PrimitiveCullMode mode, bool cullingDisabled, bool use2DPipeline)
{
    if (cullingDisabled || use2DPipeline)
    {
        return static_cast<uint32_t>(PrimitiveCullMode::Disabled);
    }
    return static_cast<uint32_t>(mode);
}

VkCullModeFlags vkCullModeForPrimitiveCullMode(PrimitiveCullMode mode, bool cullingDisabled, bool use2DPipeline)
{
    if (cullingDisabled || use2DPipeline)
    {
        return VK_CULL_MODE_NONE;
    }
    switch (mode)
    {
    case PrimitiveCullMode::Back:
        return VK_CULL_MODE_BACK_BIT;
    case PrimitiveCullMode::Disabled:
        return VK_CULL_MODE_NONE;
    case PrimitiveCullMode::Front:
        return VK_CULL_MODE_FRONT_BIT;
    }
    return VK_CULL_MODE_BACK_BIT;
}
} // namespace

void Display::createGraphicsPipeline()
{
    std::string vertPath = use2DPipeline ? "shaders/flat2d.vert.spv" : "shaders/mainforward.vert.spv";
    std::string skinnedVertPath = use2DPipeline ? vertPath : "shaders/mainforward_skinned.vert.spv";
    std::string fragPath = use2DPipeline ? "shaders/flat2d.frag.spv" : "shaders/mainforward.frag.spv";

    auto vertShaderCode = readSPIRVFile(vertPath);
    auto skinnedVertShaderCode = readSPIRVFile(skinnedVertPath);
    auto fragShaderCode = readSPIRVFile(fragPath);

    vertShaderModule = engine->createShaderModule(vertShaderCode);
    skinnedVertShaderModule = engine->createShaderModule(skinnedVertShaderCode);
    fragShaderModule = engine->createShaderModule(fragShaderCode);

    VkPipelineShaderStageCreateInfo vertShaderStageInfo{};
    vertShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    vertShaderStageInfo.stage = VK_SHADER_STAGE_VERTEX_BIT;
    vertShaderStageInfo.module = vertShaderModule;
    vertShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo fragShaderStageInfo{};
    fragShaderStageInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    fragShaderStageInfo.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    fragShaderStageInfo.module = fragShaderModule;
    fragShaderStageInfo.pName = "main";

    VkPipelineShaderStageCreateInfo shaderStages[] = {vertShaderStageInfo, fragShaderStageInfo};
    VkPipelineShaderStageCreateInfo skinnedVertShaderStageInfo = vertShaderStageInfo;
    skinnedVertShaderStageInfo.module = skinnedVertShaderModule;
    VkPipelineShaderStageCreateInfo skinnedShaderStages[] = {skinnedVertShaderStageInfo, fragShaderStageInfo};

    auto bindingDescription = Vertex::getBindingDescription();
    auto skinnedAttributeDescriptions = Vertex::getAttributeDescriptions();
    auto nonSkinnedAttributeDescriptions = Vertex::getNonSkinnedAttributeDescriptions();

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputInfo.vertexBindingDescriptionCount = 1;
    vertexInputInfo.pVertexBindingDescriptions = &bindingDescription;
    vertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(nonSkinnedAttributeDescriptions.size());
    vertexInputInfo.pVertexAttributeDescriptions = nonSkinnedAttributeDescriptions.data();

    VkPipelineVertexInputStateCreateInfo skinnedVertexInputInfo = vertexInputInfo;
    skinnedVertexInputInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(skinnedAttributeDescriptions.size());
    skinnedVertexInputInfo.pVertexAttributeDescriptions = skinnedAttributeDescriptions.data();

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    std::vector<VkDynamicState> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};

    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    VkViewport viewport{};
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = 800.0f;
    viewport.height = 600.0f;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {800, 600};

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = &viewport;
    viewportState.scissorCount = 1;
    viewportState.pScissors = &scissor;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vkCullModeForPrimitiveCullMode(PrimitiveCullMode::Back, cullingDisabled, use2DPipeline);
    rasterizer.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable = use2DPipeline ? VK_FALSE : VK_TRUE;
    depthStencil.depthWriteEnable = use2DPipeline ? VK_FALSE : VK_TRUE;
    depthStencil.depthCompareOp = use2DPipeline ? VK_COMPARE_OP_ALWAYS : VK_COMPARE_OP_LESS;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.minDepthBounds = 0.0f;
    depthStencil.maxDepthBounds = 1.0f;
    depthStencil.stencilTestEnable = VK_FALSE;
    depthStencil.front = {};
    depthStencil.back = {};

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.sampleShadingEnable = VK_TRUE;
    multisampling.rasterizationSamples = msaaSamples;
    multisampling.minSampleShading = 0.2f;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
    colorBlendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    colorBlendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.logicOp = VK_LOGIC_OP_COPY;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;
    colorBlending.blendConstants[0] = 0.0f;
    colorBlending.blendConstants[1] = 0.0f;
    colorBlending.blendConstants[2] = 0.0f;
    colorBlending.blendConstants[3] = 0.0f;

    engine->createDescriptorSetLayouts();

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    std::array<VkDescriptorSetLayout, 2> setLayouts = {engine->descriptorSetLayout, engine->primitiveDescriptorSetLayout};
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts = setLayouts.data();
    pipelineLayoutInfo.pushConstantRangeCount = 0;
    pipelineLayoutInfo.pPushConstantRanges = nullptr;

    if (vkCreatePipelineLayout(engine->logicalDevice, &pipelineLayoutInfo, nullptr, &pipelineLayout) != VK_SUCCESS)
    {
        throw std::runtime_error("Failed to create pipeline layout!");
    }
    engine->nameVulkanObject((uint64_t)pipelineLayout, VK_OBJECT_TYPE_PIPELINE_LAYOUT, "displayPipelineLayout");

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = VK_NULL_HANDLE;
    pipelineInfo.basePipelineIndex = -1;

    VkPipelineDepthStencilStateCreateInfo transparentDepthStencil = depthStencil;
    transparentDepthStencil.depthWriteEnable = VK_FALSE;
    for (uint32_t i = 0; i < graphicsPipelines.size(); ++i)
    {
        rasterizer.cullMode = vkCullModeForPrimitiveCullMode(static_cast<PrimitiveCullMode>(i), cullingDisabled, use2DPipeline);
        pipelineInfo.pDepthStencilState = &depthStencil;
        if (vkCreateGraphicsPipelines(engine->logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &graphicsPipelines[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create graphics pipeline!");
        }

        VkGraphicsPipelineCreateInfo skinnedPipelineInfo = pipelineInfo;
        skinnedPipelineInfo.pStages = skinnedShaderStages;
        skinnedPipelineInfo.pVertexInputState = &skinnedVertexInputInfo;
        if (vkCreateGraphicsPipelines(engine->logicalDevice, VK_NULL_HANDLE, 1, &skinnedPipelineInfo, nullptr, &skinnedGraphicsPipelines[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create skinned graphics pipeline!");
        }

        pipelineInfo.pDepthStencilState = &transparentDepthStencil;
        if (vkCreateGraphicsPipelines(engine->logicalDevice, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &transparentGraphicsPipelines[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create transparent graphics pipeline!");
        }

        VkGraphicsPipelineCreateInfo transparentSkinnedPipelineInfo = pipelineInfo;
        transparentSkinnedPipelineInfo.pStages = skinnedShaderStages;
        transparentSkinnedPipelineInfo.pVertexInputState = &skinnedVertexInputInfo;
        if (vkCreateGraphicsPipelines(engine->logicalDevice, VK_NULL_HANDLE, 1, &transparentSkinnedPipelineInfo, nullptr, &transparentSkinnedGraphicsPipelines[i]) != VK_SUCCESS)
        {
            throw std::runtime_error("Failed to create transparent skinned graphics pipeline!");
        }
    }

    for (auto& camera : cameras)
    {
        camera->allocateDescriptorSet();
    }
}
