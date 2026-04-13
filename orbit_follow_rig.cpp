#include "orbit_follow_rig.h"

#include <algorithm>
#include <cmath>

namespace
{
FollowSettings sanitizedSettings(const FollowSettings& settings)
{
    return followcam::sanitizeSettings(settings);
}
}

void FollowOrbit::configure(int sceneIndex, const FollowSettings& settings)
{
    sceneIndex_ = sceneIndex;
    settings_ = sanitizedSettings(settings);
    initialized_ = false;
    headingInitialized_ = false;
}

void FollowOrbit::clear()
{
    sceneIndex_ = -1;
    settings_ = FollowSettings{};
    initialized_ = false;
    headingInitialized_ = false;
    lastTargetCenter_ = glm::vec3(0.0f);
    targetYaw_ = 0.0f;
}

bool FollowOrbit::isEnabled() const
{
    return settings_.enabled && sceneIndex_ >= 0;
}

int FollowOrbit::sceneIndex() const
{
    return sceneIndex_;
}

const FollowSettings& FollowOrbit::settings() const
{
    return settings_;
}

FollowOrbitPose FollowOrbit::update(const glm::vec3& targetCenter,
                                       const glm::vec3& targetForward,
                                       float deltaTime,
                                       const FollowOrbitPose& currentPose)
{
    FollowOrbitPose pose = currentPose;
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
        targetYaw_ = computeTargetYaw(flattenedForward);
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
            desiredTargetYaw = computeTargetYaw(flattenedForward);
        }

        const float headingLerp = std::clamp(deltaTime * 8.0f, 0.0f, 1.0f);
        const float yawDelta = normalizeAngle(desiredTargetYaw - targetYaw_);
        targetYaw_ = normalizeAngle(targetYaw_ + yawDelta * headingLerp);
        lastTargetCenter_ = targetCenter;
    }

    const FollowOrbitPose desiredPose = computePose(targetCenter, targetYaw_, settings_);
    const float dist = std::max(followcam::kMinDistance, settings_.distance);
    glm::vec3 desiredDirection = desiredPose.position - targetCenter;
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
        pose = desiredPose;
        initialized_ = true;
    }

    const float smoothSpeed = std::max(settings_.smoothSpeed, 0.0f);
    const float t = std::clamp(1.0f - std::exp(-smoothSpeed * std::max(deltaTime, 0.0f)), 0.0f, 1.0f);
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

    // Damped camera rotation to avoid snap/jitter when the target heading changes.
    const float yawDelta = normalizeAngle(desiredPose.rotation.x - pose.rotation.x);
    pose.rotation.x = normalizeAngle(pose.rotation.x + yawDelta * t);
    pose.rotation.y = glm::mix(pose.rotation.y, desiredPose.rotation.y, t);

    return pose;
}

FollowOrbitPose FollowOrbit::computePose(const glm::vec3& targetCenter,
                                            float targetYaw,
                                            const FollowSettings& settings)
{
    constexpr float kPi = 3.14159265358979323846f;
    const FollowSettings sanitized = sanitizedSettings(settings);
    const float pitch = sanitized.relativePitch;
    // Follow-camera convention: relativeYaw=0 means "behind target".
    const float worldYaw = normalizeAngle(targetYaw + sanitized.relativeYaw + kPi);
    const float dist = sanitized.distance;
    const glm::vec3 desiredDirection = computeOrbitDirection(worldYaw, pitch);

    FollowOrbitPose pose;
    pose.position = targetCenter + (desiredDirection * dist);

    const glm::vec3 toTarget = targetCenter - pose.position;
    if (glm::length(toTarget) > 0.001f)
    {
        const glm::vec3 front = glm::normalize(toTarget);
        float yaw = std::atan2(front.x, -front.z);
        float pitchRadians = std::asin(glm::clamp(front.y, -1.0f, 1.0f));
        pose.rotation = glm::vec2(normalizeAngle(yaw), pitchRadians);
    }

    return pose;
}

float FollowOrbit::computeTargetYaw(const glm::vec3& targetForward)
{
    glm::vec3 flattenedForward(targetForward.x, 0.0f, targetForward.z);
    if (glm::length(flattenedForward) < 0.0001f)
    {
        flattenedForward = glm::vec3(0.0f, 0.0f, 1.0f);
    }
    else
    {
        flattenedForward = glm::normalize(flattenedForward);
    }
    return std::atan2(flattenedForward.x, flattenedForward.z);
}

glm::vec3 FollowOrbit::computeOrbitDirection(float worldYaw, float pitch)
{
    glm::vec3 desiredDirection;
    desiredDirection.x = std::sin(worldYaw) * std::cos(pitch);
    desiredDirection.y = std::sin(pitch);
    desiredDirection.z = std::cos(worldYaw) * std::cos(pitch);
    if (glm::length(desiredDirection) < 0.0001f)
    {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return glm::normalize(desiredDirection);
}

float FollowOrbit::normalizeAngle(float angle)
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
