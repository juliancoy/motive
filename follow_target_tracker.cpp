#include "follow_target_tracker.h"

#include <algorithm>
#include <cmath>

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
        }
    }

    frame.motionCenter = smoothed_;
    return frame;
}
