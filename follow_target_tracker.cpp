#include "follow_target_tracker.h"

#include <algorithm>
#include <cmath>
#include <limits>

void FollowTargetTracker::reset()
{
    initialized_ = false;
    smoothed_ = glm::vec3(0.0f);
}

FollowTargetFrame FollowTargetTracker::update(const glm::vec3& rawCenter, float deltaTime, float smoothSpeed)
{
    FollowTargetFrame frame;
    frame.valid = true;
    frame.rawCenter = rawCenter;

    if (!initialized_)
    {
        smoothed_ = rawCenter;
        initialized_ = true;
    }
    else
    {
        const float dt = std::max(deltaTime, 0.0f);
        const float speed = std::max(smoothSpeed, 0.0f);
        if (speed <= 0.0f || dt <= 0.0f)
        {
            smoothed_ = rawCenter;
        }
        else
        {
            const float t = std::clamp(1.0f - std::exp(-speed * dt), 0.0f, 1.0f);
            smoothed_ = glm::mix(smoothed_, rawCenter, t);

            // Large world-space coordinates (eg. far-from-origin scenes) can cause
            // 1-ULP toggling in float blends, which presents as visible camera shake.
            // Snap to raw once we're within precision-scale epsilon.
            const glm::vec3 delta = rawCenter - smoothed_;
            const float maxMagnitude = std::max({
                1.0f,
                std::fabs(rawCenter.x), std::fabs(rawCenter.y), std::fabs(rawCenter.z),
                std::fabs(smoothed_.x), std::fabs(smoothed_.y), std::fabs(smoothed_.z)
            });
            const float snapEpsilon = std::max(1e-5f, 2.0f * std::numeric_limits<float>::epsilon() * maxMagnitude);
            if (glm::dot(delta, delta) <= (snapEpsilon * snapEpsilon))
            {
                smoothed_ = rawCenter;
            }
        }
    }

    frame.motionCenter = smoothed_;
    return frame;
}
