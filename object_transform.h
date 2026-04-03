#pragma once

#include <cstdint>
#include <glm/glm.hpp>

// Maximum number of per-primitive instances supported via the ObjectUBO. Keep in sync with shaders.
constexpr uint32_t kMaxPrimitiveInstances = 128;

struct ObjectTransform
{
    glm::mat4 model = glm::mat4(1.0f);
    glm::uvec4 instanceData = glm::uvec4(1, 0, 0, 0); // x = active instance count
    glm::uvec4 yuvParams = glm::uvec4(1, 1, 8, 0);
    glm::uvec4 materialFlags = glm::uvec4(0, 0, 0, 0); // x = alpha mode
    glm::vec4 materialParams = glm::vec4(0.5f, 0.0f, 0.0f, 0.0f); // x = alpha cutoff
};
