#pragma once

#include <cstdint>
#include <glm/glm.hpp>

// Maximum number of per-primitive instances supported via the ObjectUBO.
constexpr uint32_t kMaxPrimitiveInstances = 16;

struct ObjectTransform
{
    glm::mat4 model = glm::mat4(1.0f);
    glm::vec4 instanceOffsets[kMaxPrimitiveInstances]{};
    glm::uvec4 instanceData = glm::uvec4(1, 0, 0, 0); // x = active instance count
};
