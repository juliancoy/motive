# Motive Architecture (Current Implementation)

This document describes what the repository currently implements.

## 1. Build Graph

Runtime code is split into static libraries with explicit ownership:

- `motive_vendor_ufbx`: third-party FBX parser (`ufbx.c`).
- `motive_runtime_camera`: camera/follow/input runtime (`camera`, `orbit_follow_rig`, `follow_target_tracker`, `input_router`).
- `motive_runtime_scene`: animation/import/model/primitive/texture runtime.
- `engine`: Vulkan/device/display/editor integration plus orchestration.
- App executables: `motive3d`, `motive3d_runtime`, `motive2d`, `encode`, `motive_editor`, `asset_load_profile`.

Dependency direction:

- `motive_runtime_scene -> motive_runtime_camera + motive_vendor_ufbx`
- `engine -> motive_runtime_camera + motive_runtime_scene + motive_vendor_ufbx`
- `apps -> engine`

All app executables are linked through `engine` in `CMakeLists.txt`.

## 2. Runtime Ownership

- `Engine` owns global runtime systems (device/context, resource managers, models, physics world/backend selection).
- `Display` owns rendering lifecycle and per-frame runtime controller updates (`Display::updateRuntimeControllers`).
- `ViewportHostWidget` owns editor intent/state mutation and bridges UI/REST commands into runtime config.
- `EngineUiControlServer` owns REST transport; business logic is delegated back through command/profile callbacks.

## 3. Camera/Follow Ownership and Invariants

- Camera mode source of truth is `Camera::mode` and is changed via `setMode`.
- Follow target source of truth is camera follow state (`setFollowTarget`, `getFollowSceneIndex`, `isFollowModeEnabled`).
- `setFollowTarget(sceneIndex, ...)` does not auto-switch mode for either assignment or clear.
- Follow-camera lookup is explicit and state-only (`findFollowCameraIndexForScene`).

## 4. Character Control Ownership

- Single-owner policy is enforced: enabling control for one scene item disables others.
- Input/key state for non-owner characters is cleared when ownership changes.
- Current UI behavior: selecting a scene item in hierarchy does not change character ownership; it may reframe only when focused viewport camera is in `FreeFly`.
- Camera node selection updates active camera context but does not directly set character ownership.

## 5. Update Flow (Follow + Motion)

1. UI/REST sets desired state (camera mode, follow target/settings, controllable owner, animation/physics coupling).
2. Runtime frame tick (`Display::updateRuntimeControllers`) applies input, character motion, and camera follow/orbit updates.
3. Follow target tracking uses `follow_target_tracker` split outputs:
   - `rawCenter` for framing/look-at lock.
   - `motionCenter` for damped orbit/follow position integration.
4. Viewport/editor layer samples runtime telemetry (`sceneProfileJson`, motion debug frame/summary/history) for UI + REST.

## 6. UI/REST Surfaces

- Explicit profile endpoints:
  - `GET /profile/scene_state`
  - `GET /profile/camera_state`
  - `GET /profile/viewport_state`
  - `GET /profile/motion_state`
  - `GET /profile/input_state`
  - `GET /profile/hierarchy_state`
- Hierarchy route:
  - `GET /hierarchy` returns hierarchy tree plus settings snapshot (`sceneItems`, camera list/tracking, motion overlay, performance).
  - `POST /hierarchy` applies hierarchy-linked updates via command forwarding (`scene_item`, `camera`, `animation`, `character`, `physics_coupling`, `physics_gravity`, `selection`, `rebuild`, `reset`).
- Motion debug endpoints:
  - `GET /debug/motion/frame`
  - `GET /debug/motion/summary`
  - `GET /debug/motion/history`
  - `GET /debug/motion/overlay`
  - `POST /controls/debug_motion`
  - `POST /controls/debug_motion_overlay`
- Inspector Runtime tab includes follow/kinematic/animation runtime info and motion overlay toggles.

## 7. Executable Roles

- `motive3d`: shell/editor entrypoint (`main.cpp` -> `runMotiveEditorApp`).
- `motive_editor`: compatibility alias; same editor entrypoint pattern.
- `motive3d_runtime`: standalone runtime app (`motive3d.cpp` + `motive3d_app.cpp`).
- `motive2d`: 2D/video-focused runtime.
- `encode`: headless decode/encode path.

## 8. Testing Surface

Automated tests currently present under `tests/`:

- Unit tests for camera mode rules, follow settings/tracker, orbit rig, control server, inspector UI.
- Integration REST test: `tests/integration/test_rest_animation_physics.py`.

Repository also includes script-based runtime/API regression helpers (`test_follow_cam.py`, `test_follow_cam.sh`, `test_character_animation_flow.sh`, etc.).
