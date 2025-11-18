#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(set = 0, binding = 0) uniform CameraUBO {
    mat4 view;
    mat4 proj;
} cameraUBO;

layout(set = 1, binding = 0) uniform ObjectUBO {
    mat4 model;
} objectUBO;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragNormal;
layout(location = 1) out vec2 fragTexCoord;

void main() {
    gl_Position = cameraUBO.proj * cameraUBO.view * objectUBO.model * vec4(inPosition, 1.0);
    fragNormal = mat3(objectUBO.model) * inNormal;
    fragTexCoord = inTexCoord;
}
