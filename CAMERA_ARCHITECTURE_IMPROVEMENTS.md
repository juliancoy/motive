# Camera Architecture Improvements

## Implementation Complete (2024-04-14)

This document describes the camera and input routing architecture as implemented.

## Architecture Overview

### Camera Mode State Machine

```cpp
enum class CameraMode {
    FreeFly,           // WASD moves camera directly
    CharacterFollow,   // WASD moves character, camera follows automatically
    OrbitFollow,      // Camera orbits target (right-drag to orbit)
    Fixed            // Camera position fixed (no movement)
};
```

### Input Flow

```
GLFW Window
    │
    ▼
Display::handleKey() / handleMouseButton()
    │
    ├──► InputRouter::handleKey()     ──► Character input state
    │
    └──► Camera::handleKey()        ──► Camera-specific keys (P/O/R)
        │
        └──► Camera::handleMouseButton() / handleCursorPos()  ──► Orbit rotation
```

### Input Router Design

- `InputRouter` handles WASD/Q/E input for all camera modes
- Queries `Camera::getMode()` directly from `Display` to determine behavior
- In `CharacterFollow` mode: routes input to character (via `Model::setCharacterInput()`)
- In `FreeFly` mode: modifies `cameraPos` directly via reference parameter
- `Display` holds `InputRouter` instance and calls `setDisplay(this)` at construction

```cpp
// InputRouter handles both character and camera movement based on Camera mode
void InputRouter::update(float deltaTime, const glm::vec3& cameraRotation, glm::vec3& inout_cameraPos)
{
    Camera* camera = m_display->getActiveCamera();
    const bool isFollowMode = camera->getMode() == CameraMode::CharacterFollow;
    
    if (isFollowMode) {
        // Route input to character
        m_characterTarget->setCharacterInput(inputDir);
    } else {
        // Move camera directly
        inout_cameraPos += moveDir * moveSpeed;
    }
}
```

### Host Widget Integration

`ViewportHostWidget` queries camera mode directly via `isFreeFlyCameraEnabled()`:

```cpp
bool ViewportHostWidget::isFreeFlyCameraEnabled() const
{
    Camera* cam = m_runtime->camera();
    return cam && cam->getMode() == CameraMode::FreeFly;
}
```

No duplicate mode state stored - always queries Camera directly.

## Key Classes

| Class | Responsibility |
|-------|----------------|
| `Camera` | View/projection matrices, follow orbit, camera-specific input |
| `InputRouter` | WASD input routing based on Camera mode |
| `Display` | Window, swapchain, InputRouter ownership |
| `FollowOrbit` | Target tracking by scene index |
| `ViewportHostWidget` | Qt viewport, queries Camera for mode |

## Files Modified

### Primary:
- `camera.h` / `camera.cpp` - CameraMode enum, setMode()
- `input_router.h` / `input_router.cpp` - Queries Camera from Display
- `host_widget.h` / `host_widget.cpp` - Removed duplicate m_freeFlyCameraEnabled
- `display_core.cpp` - Calls inputRouter->setDisplay(this)

### Architecture Decisions

1. **FreeFly movement in InputRouter**: Intentional design choice - keeps all WASD routing in one place
2. **No duplicate state**: host_widget queries Camera directly, no local mode storage
3. **Display reference**: InputRouter holds Display* to query active Camera

## Expected Behavior by Mode

| Mode | WASD | Right-drag | Q/E |
|-----|------|------------|-----|
| FreeFly | Move camera | Rotate view | Move up/down |
| CharacterFollow | Move character | Orbit around character | - |
| OrbitFollow | - | Orbit around target | - |
| Fixed | - | - | - |