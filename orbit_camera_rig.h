#pragma once

#include <glm/glm.hpp>

#include "camera_follow_settings.h"

struct OrbitCameraPose
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec2 rotation = glm::vec2(0.0f);
};

class OrbitCameraRig
{
public:
    void configure(int sceneIndex, const FollowSettings& settings);
    void clear();

    bool isEnabled() const;
    int sceneIndex() const;
    const FollowSettings& settings() const;

    OrbitCameraPose update(const glm::vec3& targetCenter,
                           const glm::vec3& targetForward,
                           float deltaTime,
                           const OrbitCameraPose& currentPose);

private:
    static float normalizeAngle(float angle);

    int sceneIndex_ = -1;
    FollowSettings settings_;
    bool initialized_ = false;
    bool headingInitialized_ = false;
    glm::vec3 lastTargetCenter_ = glm::vec3(0.0f);
    float targetYaw_ = 0.0f;
};
