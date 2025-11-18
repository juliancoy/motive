#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_ARB_shading_language_420pack : enable

layout(location = 0) in vec3 fragNormal;
layout(location = 1) in vec2 fragTexCoord;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 1) uniform sampler2D texSampler;

void main() {
    // Simple diffuse lighting with light coming from camera
    vec3 lightDir = vec3(0.0, 0.0, 1.0);
    float diff = max(dot(normalize(fragNormal), lightDir), 0.0);
    vec3 lighting = vec3(0.1) + diff * vec3(0.9); // Ambient + diffuse
    
    // Sample texture or use fallback color
    outColor = texture(texSampler, fragTexCoord) * vec4(lighting, 1.0);
    if (outColor.a == 0.0) {
        outColor = vec4(fragNormal * 0.5 + 0.5, 1.0); // Fallback: normal as color
    }
}
