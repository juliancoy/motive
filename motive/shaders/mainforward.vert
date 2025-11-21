#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} cameraUBO;

const uint MAX_INSTANCE_COUNT = 16;

layout(set = 1, binding = 0) uniform ObjectUBO {
    mat4 model;
    vec4 instanceOffsets[MAX_INSTANCE_COUNT];
    uvec4 instanceData;
} objectUBO;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    vec3 instanceOffset = vec3(0.0);
    if (gl_InstanceIndex < objectUBO.instanceData.x) {
        instanceOffset = objectUBO.instanceOffsets[gl_InstanceIndex].xyz;
    }

    mat4 instanceTransform = mat4(1.0);
    instanceTransform[3].xyz = instanceOffset;

    vec4 worldPosition = instanceTransform * objectUBO.model * vec4(inPosition, 1.0);
    gl_Position = cameraUBO.proj * cameraUBO.view * worldPosition;
    fragNormal = mat3(objectUBO.model) * inNormal;
    fragTexCoord = inTexCoord;
}
