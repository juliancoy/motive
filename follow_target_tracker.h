#pragma once

#include <glm/glm.hpp>

struct FollowTargetFrame
{
    bool valid = false;
    glm::vec3 rawCenter = glm::vec3(0.0f);
    glm::vec3 motionCenter = glm::vec3(0.0f);
};

// Owns temporal filtering of follow targets.
// rawCenter: exact target center for visual lock.
// motionCenter: smoothed target center for orbit/follow motion stability.
class FollowTargetTracker
{
public:
    void reset();
    FollowTargetFrame update(const glm::vec3& rawCenter, float deltaTime, float smoothSpeed);

private:
    bool initialized_ = false;
    glm::vec3 smoothed_ = glm::vec3(0.0f);
};

