#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform sampler2D texSampler;
layout(set = 0, binding = 1) uniform LightUBO {
    vec4 direction;
    vec4 ambient;
    vec4 diffuse;
} lightUBO;

void main() {
    vec3 normal = normalize(fragNormal);
    vec3 lightDir = normalize(lightUBO.direction.xyz);
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 lighting = lightUBO.ambient.xyz + diff * lightUBO.diffuse.xyz;
    
    // Sample texture or use fallback color
    outColor = texture(texSampler, fragTexCoord) * vec4(lighting, 1.0);
    if (outColor.a == 0.0) {
        outColor = vec4(fragNormal * 0.5 + 0.5, 1.0); // Fallback: normal as color
    }
}
