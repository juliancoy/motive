#pragma once

#include "camera_mode.h"

namespace input_mode_rules
{
inline bool shouldRouteCharacterInput(CameraMode mode, bool hasCharacterTarget)
{
    return (mode == CameraMode::CharacterFollow || mode == CameraMode::OrbitFollow) && hasCharacterTarget;
}

inline bool shouldMoveCamera(CameraMode mode, bool controlsEnabled)
{
    return mode == CameraMode::FreeFly && controlsEnabled;
}
} // namespace input_mode_rules
