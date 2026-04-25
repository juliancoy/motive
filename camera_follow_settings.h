#pragma once

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

struct FollowSettings
{
    float relativeYaw = 0.0f;
    float relativePitch = 0.3f;
    float distance = 5.0f;
    float smoothSpeed = 10.0f;
    glm::vec3 targetOffset = glm::vec3(0.0f, 0.0f, 0.0f);
    bool enabled = false;
};

namespace followcam
{
inline constexpr float kMinDistance = 1.0f;
inline constexpr float kMaxPitchRadians = 1.4f;
inline constexpr float kMinSmoothSpeed = 0.0f;
inline constexpr float kDefaultSmoothSpeed = 10.0f;

inline float normalizeAngleRadians(float radians)
{
    return std::atan2(std::sin(radians), std::cos(radians));
}

inline FollowSettings sanitizeSettings(const FollowSettings& settings)
{
    FollowSettings sanitized = settings;
    sanitized.distance = std::max(kMinDistance, sanitized.distance);
    sanitized.relativePitch = std::clamp(sanitized.relativePitch, -kMaxPitchRadians, kMaxPitchRadians);
    sanitized.smoothSpeed = std::max(kMinSmoothSpeed, sanitized.smoothSpeed);
    sanitized.relativeYaw = normalizeAngleRadians(sanitized.relativeYaw);
    return sanitized;
}
}
