# Motive Architecture (Simplified)

## 1. Build Graph

Runtime code is split by ownership:

- `motive_vendor_ufbx`: third-party FBX parser only.
- `motive_runtime_camera`: camera modes, follow rig, input router.
- `motive_runtime_scene`: model/mesh/primitive/texture/animation/import.
- `engine`: Vulkan/device/display/editor integration, physics/video orchestration.
- apps: `motive3d`, `motive3d_runtime`, `motive2d`, `encode`, `motive_editor`, `asset_load_profile`.

Dependency direction:

- `motive_runtime_scene -> motive_runtime_camera + motive_vendor_ufbx`
- `engine -> motive_runtime_camera + motive_runtime_scene + motive_vendor_ufbx`
- `apps -> engine`

No app target should directly own scene/camera runtime internals.

## 2. Runtime Ownership

- `Engine` owns: device/context, global GPU resources, model list, display instances, physics backend.
- `Display` owns: render loop plumbing, camera runtime controller updates, swapchain/window lifecycle.
- `ViewportHostWidget` owns: editor UI intent and scene-edit operations, not per-frame camera motion logic.

## 3. Non-Negotiable Invariants

### Camera/Follow

- Camera mode source of truth: `Camera::mode` (`setMode` only).
- Follow target source of truth: camera follow state (`setFollowTarget`, `getFollowSceneIndex`).
- `setFollowTarget` does not implicitly switch mode.
- Follow-camera identity is target-state based, not name based.

### Character Control

- Single-owner policy: at most one scene model is controllable at a time.
- Enabling controllable on one model clears controllable/input state on all others.
- Camera selection/activation must not implicitly toggle character controllability.

### Update Order

- Follow transform application happens in the runtime tick owner (`Display::updateRuntimeControllers` path).
- UI/REST handlers may change configuration/state, but should not force per-frame follow transform updates.

## 4. Follow-Cam Flow

1. Resolve target scene index.
2. Resolve/reuse follow camera by follow target state.
3. Configure follow settings (`distance/yaw/pitch/smoothing`).
4. Explicitly choose mode (`CharacterFollow`, `OrbitFollow`, `Fixed`, `FreeFly`) through `setMode`.
5. Runtime tick applies motion/follow each frame.

### Follow Target Subsystem

- Module: `follow_target_tracker.{h,cpp}` (owned by `motive_runtime_camera`).
- Input: raw target center sampled from model follow anchor each frame.
- Outputs:
  - `rawCenter`: exact center for view lock (`lookAt`) and framing diagnostics.
  - `motionCenter`: damped center for orbit/follow position integration.
- Contract:
  - Camera position integrates against `motionCenter` to suppress animated-bounds jitter.
  - Camera view always targets `rawCenter` to keep object centered in screen space.
  - Reset tracker state whenever follow target is cleared/invalidated/reassigned.

This split keeps visual centering accurate while preventing high-frequency camera oscillation.

## 5. UI Ownership Rules

- Hierarchy selection changes view context (active camera/inspector context).
- Hierarchy selection should not silently mutate gameplay ownership state.
- Inspector is organized by domain:
  - `Overview`, `Visual`, `Motion`, `Camera`, `Runtime`.
- Section visibility is selection-driven (camera/light/scene item/primitive).

## 6. Executable Roles

- `motive3d`: shell/editor program (tabs/panels + REST control plane).
- `motive3d_runtime`: standalone runtime loop.
- `motive2d`: video-first runtime.
- `encode`: headless decode/encode path.
- `motive_editor`: compatibility alias for shell/editor entry.

## 7. Testing Surface

Current automated checks:

- camera mode/transition/input routing unit coverage
- REST integration coverage for animation/physics controls

Recommended next expansion:

- deterministic fixed-timestep end-to-end tests for follow-cam + physics + animation progression.
