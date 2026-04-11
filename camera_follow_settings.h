#pragma once

#include <glm/glm.hpp>

struct FollowSettings
{
    float relativeYaw = 0.0f;
    float relativePitch = 0.3f;
    float distance = 5.0f;
    float smoothSpeed = 5.0f;
    glm::vec3 targetOffset = glm::vec3(0.0f, 0.0f, 0.0f);
    bool enabled = false;
};
