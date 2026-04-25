# UI Hierarchy Snapshot (REST-Derived)

Generated: `2026-04-24T17:21:10-04:00`

Source endpoints:
- `GET /health`
- `GET /hierarchy`
- `GET /profile/ui`
- `GET /profile/scene`

## Runtime Status
- Health: `ok=true`
- REST port: `40132`
- Scene item count: `2`
- Focused viewport index: `0`

## Main Window / Layout
From `/profile/ui`:
- Splitters: `2`
- Dock widgets: `1` (Inspector dock on right)

## Hierarchy Root (Top-Level)
From `/hierarchy` (4 top-level nodes):
1. `Camera` (`type=camera`, `cameraIndex=0`)
2. `Directional Light` (`type=light`)
3. `rp_nathan_animated_003_walking` (`type=scene_item`, `sceneIndex=0`)
4. `china_scene` (`type=scene_item`, `sceneIndex=1`)

Notes:
- Follow cameras are intentionally hidden as independent hierarchy root nodes.
- Follow-cam ownership exists in camera configs and scene-item selection behavior.

## Scene Item Structure (REST)
- `rp_nathan_animated_003_walking`
  - Meshes: `1`
  - Primitives: `1`
  - Animations: `1` clip (`Take 001`)
- `china_scene`
  - Meshes: `1`
  - Primitives: `100` (`Primitive 0`..`Primitive 99`)
  - Material/texture subtree on each primitive

## Right Panel Hierarchy (Best-Practice Refactor)
The right-side Inspector (`Element` tab) is now hierarchized into focused sub-tabs:

1. `Overview`
- `Summary` section
  - Name, Source, Animation Path, Bounds Size, Texture
- `Transform` section
  - Translation, Rotation, Scale

2. `Visual`
- `Material & Mesh` section
  - Mesh consolidation/import option
  - Primitive cull mode, opacity forcing
  - Paint override and color
- `Lighting` section
  - Light type, brightness, color

3. `Motion`
- `Animation` section
  - Clip, play/loop/speed, animation-physics coupling
- `Physics & Motion` section
  - Gravity mode, custom gravity, turn responsiveness

4. `Camera`
- `Camera` section
  - Free-fly mode, near/far clip, follow target, follow distance/yaw/pitch/damping

5. `Runtime`
- `Runtime Diagnostics` section
  - Object follow-cam ownership/details
  - Kinematic runtime state (controllable/grounded/input/velocity)
  - Animation runtime state (current state/speed/blend)

## Context Rules
Inspector content remains selection-aware:
- Camera selection focuses camera controls.
- Light selection focuses lighting controls.
- Scene item selection defaults to `Overview` and enables object-relevant sections.
- Primitive-specific controls remain hidden unless a primitive node is selected.

## Rework Baseline
This structure is the baseline for further right-panel UX work:
- clear domain separation (scene, visual, motion, camera, diagnostics)
- reduced cognitive load vs single long mixed form
- scalable for high-primitive assets (e.g., `china_scene`)
