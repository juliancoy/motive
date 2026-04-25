#include "orbit_follow_rig.h"

#include <algorithm>
#include <cmath>

namespace
{
FollowSettings sanitizedSettings(const FollowSettings& settings)
{
    return followcam::sanitizeSettings(settings);
}

glm::vec2 rotationFromLookAt(const glm::vec3& cameraPosition, const glm::vec3& targetCenter)
{
    const glm::vec3 toTarget = targetCenter - cameraPosition;
    if (glm::length(toTarget) <= 0.001f)
    {
        return glm::vec2(0.0f);
    }

    const glm::vec3 front = glm::normalize(toTarget);
    const float yaw = std::atan2(-front.x, -front.z);
    const float pitch = std::asin(glm::clamp(front.y, -1.0f, 1.0f));
    return glm::vec2(yaw, pitch);
}
}

void CharacterFollowRig::configure(int sceneIndex, const FollowSettings& settings)
{
    const bool sceneChanged = (sceneIndex_ != sceneIndex);
    sceneIndex_ = sceneIndex;
    settings_ = sanitizedSettings(settings);
    if (sceneChanged)
    {
        initialized_ = false;
        headingInitialized_ = false;
    }
}

void CharacterFollowRig::clear()
{
    sceneIndex_ = -1;
    settings_ = FollowSettings{};
    initialized_ = false;
    headingInitialized_ = false;
    lastTargetCenter_ = glm::vec3(0.0f);
    targetYaw_ = 0.0f;
}

bool CharacterFollowRig::isEnabled() const
{
    return settings_.enabled && sceneIndex_ >= 0;
}

int CharacterFollowRig::sceneIndex() const
{
    return sceneIndex_;
}

const FollowSettings& CharacterFollowRig::settings() const
{
    return settings_;
}

FollowOrbitPose CharacterFollowRig::update(const glm::vec3& targetCenter,
                                           const glm::vec3& targetForward,
                                           float deltaTime,
                                           const FollowOrbitPose& currentPose)
{
    (void)deltaTime;
    (void)currentPose;
    FollowOrbitPose pose = currentPose;
    if (!isEnabled())
    {
        return pose;
    }

    if (!headingInitialized_)
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
        targetYaw_ = computeTargetYaw(flattenedForward);
        lastTargetCenter_ = targetCenter;
        headingInitialized_ = true;
    }
    else
    {
        // Preserve orbit anchor heading in CharacterFollow mode to avoid
        // camera/input feedback oscillation when movement is camera-relative.
        lastTargetCenter_ = targetCenter;
    }

    const FollowOrbitPose desiredPose = computePose(targetCenter, targetYaw_, settings_);
    // CharacterFollow is a strict orbit lock around the target center.
    // Avoid directional lag here to prevent visual swinging around the subject.
    pose = desiredPose;
    initialized_ = true;
    return pose;
}

FollowOrbitPose CharacterFollowRig::computePose(const glm::vec3& targetCenter,
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
        float yaw = std::atan2(-front.x, -front.z);
        float pitchRadians = std::asin(glm::clamp(front.y, -1.0f, 1.0f));
        pose.rotation = glm::vec2(normalizeAngle(yaw), pitchRadians);
    }

    return pose;
}

float CharacterFollowRig::computeTargetYaw(const glm::vec3& targetForward)
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

glm::vec3 CharacterFollowRig::computeOrbitDirection(float worldYaw, float pitch)
{
    glm::vec3 desiredDirection;
    desiredDirection.x = -std::sin(worldYaw) * std::cos(pitch);
    desiredDirection.y = std::sin(pitch);
    desiredDirection.z = std::cos(worldYaw) * std::cos(pitch);
    if (glm::length(desiredDirection) < 0.0001f)
    {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return glm::normalize(desiredDirection);
}

float CharacterFollowRig::normalizeAngle(float angle)
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

void OrbitRig::configure(int sceneIndex, const FollowSettings& settings)
{
    const bool sceneChanged = (sceneIndex_ != sceneIndex);
    sceneIndex_ = sceneIndex;
    settings_ = sanitizedSettings(settings);
    if (sceneChanged)
    {
        initialized_ = false;
    }
}

void OrbitRig::clear()
{
    sceneIndex_ = -1;
    settings_ = FollowSettings{};
    initialized_ = false;
}

bool OrbitRig::isEnabled() const
{
    return settings_.enabled && sceneIndex_ >= 0;
}

int OrbitRig::sceneIndex() const
{
    return sceneIndex_;
}

const FollowSettings& OrbitRig::settings() const
{
    return settings_;
}

FollowOrbitPose OrbitRig::update(const glm::vec3& targetCenter,
                                 float deltaTime,
                                 const FollowOrbitPose& currentPose)
{
    (void)deltaTime;
    (void)currentPose;
    FollowOrbitPose pose = currentPose;
    if (!isEnabled())
    {
        return pose;
    }

    const FollowOrbitPose desiredPose = computePose(targetCenter, settings_);
    // OrbitFollow also uses strict center lock for deterministic framing.
    pose = desiredPose;
    initialized_ = true;
    return pose;
}

FollowOrbitPose OrbitRig::computePose(const glm::vec3& targetCenter,
                                      const FollowSettings& settings)
{
    constexpr float kPi = 3.14159265358979323846f;
    const FollowSettings sanitized = sanitizedSettings(settings);
    const float worldYaw = normalizeAngle(sanitized.relativeYaw + kPi);
    const float pitch = sanitized.relativePitch;
    const float dist = sanitized.distance;
    const glm::vec3 desiredDirection = computeOrbitDirection(worldYaw, pitch);

    FollowOrbitPose pose;
    pose.position = targetCenter + (desiredDirection * dist);

    const glm::vec3 toTarget = targetCenter - pose.position;
    if (glm::length(toTarget) > 0.001f)
    {
        const glm::vec3 front = glm::normalize(toTarget);
        float yaw = std::atan2(-front.x, -front.z);
        float pitchRadians = std::asin(glm::clamp(front.y, -1.0f, 1.0f));
        pose.rotation = glm::vec2(normalizeAngle(yaw), pitchRadians);
    }

    return pose;
}

glm::vec3 OrbitRig::computeOrbitDirection(float worldYaw, float pitch)
{
    glm::vec3 desiredDirection;
    desiredDirection.x = -std::sin(worldYaw) * std::cos(pitch);
    desiredDirection.y = std::sin(pitch);
    desiredDirection.z = std::cos(worldYaw) * std::cos(pitch);
    if (glm::length(desiredDirection) < 0.0001f)
    {
        return glm::vec3(0.0f, 0.0f, 1.0f);
    }
    return glm::normalize(desiredDirection);
}

float OrbitRig::normalizeAngle(float angle)
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
