#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform ObjectUBO {
    mat4 model;
    uvec4 instanceData;
    uvec4 yuvParams;
    uvec4 materialFlags;
    vec4 materialParams;
    vec4 colorOverride;
} objectUBO;

layout(set = 1, binding = 1) uniform sampler2D texSampler;

void main()
{
    const uint alphaMode = objectUBO.materialFlags.x;
    const bool useColorOverride = objectUBO.materialFlags.y != 0u;
    const bool forceAlphaOne = objectUBO.materialFlags.z != 0u;
    const float alphaCutoff = objectUBO.materialParams.x;

    vec4 sampledColor = texture(texSampler, fragTexCoord);
    if (useColorOverride)
    {
        sampledColor.rgb = objectUBO.colorOverride.rgb;
    }

    if ((alphaMode == 1u || (alphaMode == 2u && useColorOverride)) && sampledColor.a < alphaCutoff)
    {
        discard;
    }

    if (forceAlphaOne || alphaMode == 0u || alphaMode == 1u || (alphaMode == 2u && useColorOverride))
    {
        sampledColor.a = 1.0;
    }

    outColor = sampledColor;
}
