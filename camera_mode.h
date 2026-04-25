#pragma once

// Camera operation modes
enum class CameraMode
{
    FreeFly,         // WASD moves camera directly
    CharacterFollow, // WASD moves character, camera follows automatically
    OrbitFollow,     // Camera orbits target (right-drag to orbit)
    Fixed            // Camera position fixed (no movement)
};

