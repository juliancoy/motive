#pragma once

#include <glm/glm.hpp>

#include "camera_follow_settings.h"

struct FollowOrbitPose
{
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec2 rotation = glm::vec2(0.0f);
};

// Character-follow rig: orbit is relative to target heading.
class CharacterFollowRig
{
public:
    void configure(int sceneIndex, const FollowSettings& settings);
    void clear();

    bool isEnabled() const;
    int sceneIndex() const;
    const FollowSettings& settings() const;

    FollowOrbitPose update(const glm::vec3& targetCenter,
                           const glm::vec3& targetForward,
                           float deltaTime,
                           const FollowOrbitPose& currentPose);
    static FollowOrbitPose computePose(const glm::vec3& targetCenter,
                                       float targetYaw,
                                       const FollowSettings& settings);
    static float computeTargetYaw(const glm::vec3& targetForward);

private:
    static glm::vec3 computeOrbitDirection(float worldYaw, float pitch);
    static float normalizeAngle(float angle);

    int sceneIndex_ = -1;
    FollowSettings settings_;
    bool initialized_ = false;
    bool headingInitialized_ = false;
    glm::vec3 lastTargetCenter_ = glm::vec3(0.0f);
    float targetYaw_ = 0.0f;
};

// Orbit rig: heading-independent world-space orbit around target.
class OrbitRig
{
public:
    void configure(int sceneIndex, const FollowSettings& settings);
    void clear();

    bool isEnabled() const;
    int sceneIndex() const;
    const FollowSettings& settings() const;

    FollowOrbitPose update(const glm::vec3& targetCenter,
                           float deltaTime,
                           const FollowOrbitPose& currentPose);
    static FollowOrbitPose computePose(const glm::vec3& targetCenter,
                                       const FollowSettings& settings);

private:
    static glm::vec3 computeOrbitDirection(float worldYaw, float pitch);
    static float normalizeAngle(float angle);

    int sceneIndex_ = -1;
    FollowSettings settings_;
    bool initialized_ = false;
};

// Backward-compatible alias.
using FollowOrbit = CharacterFollowRig;
