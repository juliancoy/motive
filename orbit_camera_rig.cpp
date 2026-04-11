#include "orbit_camera_rig.h"

#include <algorithm>
#include <cmath>

void OrbitCameraRig::configure(int sceneIndex, const FollowSettings& settings)
{
    sceneIndex_ = sceneIndex;
    settings_ = settings;
    initialized_ = false;
    headingInitialized_ = false;
}

void OrbitCameraRig::clear()
{
    sceneIndex_ = -1;
    settings_ = FollowSettings{};
    initialized_ = false;
    headingInitialized_ = false;
    lastTargetCenter_ = glm::vec3(0.0f);
    targetYaw_ = 0.0f;
}

bool OrbitCameraRig::isEnabled() const
{
    return settings_.enabled && sceneIndex_ >= 0;
}

int OrbitCameraRig::sceneIndex() const
{
    return sceneIndex_;
}

const FollowSettings& OrbitCameraRig::settings() const
{
    return settings_;
}

OrbitCameraPose OrbitCameraRig::update(const glm::vec3& targetCenter,
                                       const glm::vec3& targetForward,
                                       float deltaTime,
                                       const OrbitCameraPose& currentPose)
{
    OrbitCameraPose pose = currentPose;
    if (!isEnabled())
    {
        return pose;
    }

    glm::vec3 flattenedForward(targetForward.x, 0.0f, targetForward.z);
    if (glm::length(flattenedForward) < 0.0001f)
    {
        flattenedForward = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    else
    {
        flattenedForward = glm::normalize(flattenedForward);
    }

    if (!headingInitialized_)
    {
        targetYaw_ = std::atan2(flattenedForward.x, flattenedForward.z);
        lastTargetCenter_ = targetCenter;
        headingInitialized_ = true;
    }
    else
    {
        const glm::vec3 delta = targetCenter - lastTargetCenter_;
        const glm::vec2 horizontalDelta(delta.x, delta.z);
        float desiredTargetYaw = targetYaw_;
        if (glm::length(horizontalDelta) > 0.0005f)
        {
            desiredTargetYaw = std::atan2(delta.x, delta.z);
        }
        else
        {
            desiredTargetYaw = std::atan2(flattenedForward.x, flattenedForward.z);
        }

        const float headingLerp = std::clamp(deltaTime * 8.0f, 0.0f, 1.0f);
        const float yawDelta = normalizeAngle(desiredTargetYaw - targetYaw_);
        targetYaw_ = normalizeAngle(targetYaw_ + yawDelta * headingLerp);
        lastTargetCenter_ = targetCenter;
    }

    const float pitch = std::clamp(settings_.relativePitch, -1.4f, 1.4f);
    const float worldYaw = normalizeAngle(targetYaw_ + settings_.relativeYaw);
    const float dist = std::max(0.001f, settings_.distance);

    glm::vec3 desiredDirection;
    desiredDirection.x = std::sin(worldYaw) * std::cos(pitch);
    desiredDirection.y = std::sin(pitch);
    desiredDirection.z = std::cos(worldYaw) * std::cos(pitch);
    if (glm::length(desiredDirection) < 0.0001f)
    {
        desiredDirection = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    else
    {
        desiredDirection = glm::normalize(desiredDirection);
    }

    if (!initialized_)
    {
        pose.position = targetCenter + (desiredDirection * dist);
        initialized_ = true;
    }

    const float t = std::min(settings_.smoothSpeed * deltaTime, 1.0f);
    glm::vec3 currentDirection = pose.position - targetCenter;
    if (glm::length(currentDirection) < 0.0001f)
    {
        currentDirection = desiredDirection;
    }
    else
    {
        currentDirection = glm::normalize(currentDirection);
    }

    glm::vec3 blendedDirection = glm::mix(currentDirection, desiredDirection, t);
    if (glm::length(blendedDirection) < 0.0001f)
    {
        blendedDirection = desiredDirection;
    }
    else
    {
        blendedDirection = glm::normalize(blendedDirection);
    }

    pose.position = targetCenter + (blendedDirection * dist);

    const glm::vec3 toTarget = targetCenter - pose.position;
    if (glm::length(toTarget) > 0.001f)
    {
        const glm::vec3 front = glm::normalize(toTarget);
        float yaw = std::atan2(front.x, front.z) + 3.14159f;
        float pitchRadians = -std::asin(glm::clamp(front.y, -1.0f, 1.0f));
        yaw = normalizeAngle(yaw);
        pose.rotation = glm::vec2(yaw, pitchRadians);
    }

    return pose;
}

float OrbitCameraRig::normalizeAngle(float angle)
{
    constexpr float kPi = 3.14159265358979323846f;
    constexpr float kTwoPi = 2.0f * kPi;
    while (angle > kPi)
    {
        angle -= kTwoPi;
    }
    while (angle < -kPi)
    {
        angle += kTwoPi;
    }
    return angle;
}
