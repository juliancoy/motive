#pragma once

#include "camera_mode.h"

namespace camera_mode_rules
{
inline bool requiresFollowTarget(CameraMode mode)
{
    return mode == CameraMode::CharacterFollow || mode == CameraMode::OrbitFollow;
}

inline bool canEnterMode(CameraMode mode, bool followEnabled, int followSceneIndex)
{
    if (!requiresFollowTarget(mode))
    {
        return true;
    }
    return followEnabled && followSceneIndex >= 0;
}
} // namespace camera_mode_rules

