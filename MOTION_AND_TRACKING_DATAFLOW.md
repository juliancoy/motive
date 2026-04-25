# Motion And Tracking Dataflow

This document describes the current runtime dataflow for character motion, animation preprocessing, follow-anchor selection, camera tracking, and REST/UI observability in `motive3d`.

## Scope
- Runtime path for the Qt shell + embedded viewport.
- Input ownership and source-of-truth rules.
- Character motion + animation update order.
- Follow camera target computation and smoothing.
- REST endpoints and debug fields that expose the pipeline.

## Primary Runtime Loop (Editor)
Frame tick originates in:
- `ViewportHostWidget::renderFrame()` in `host_widget.cpp`.

Per-frame order is:
1. Update controllable character physics intent and motion.
2. Apply/advance animation state.
3. Step global physics (`engine->updatePhysics(dt)`).
4. Update input routing + follow cameras (`display->updateRuntimeControllers(dt)`).
5. Render (`m_runtime->render()`).

This ordering is deliberate: camera follow reads post-animation/post-motion target state in the same frame.

## Input Ownership And Routing
### Entry points
- Physical keyboard events flow through `InputRouter`.
- REST synthetic input flows through `POST /controls/character` -> `ViewportHostWidget::injectCharacterInput(...)`.

### Input router
`Display::updateRuntimeControllers(float dt)` does:
- Resolve active camera and mode.
- Select character target for the router when camera mode is follow.
- Call `inputRouter->update(...)` with camera yaw/pitch and camera position reference.

`InputRouter` rules:
- In `CharacterFollow`, WASD is routed to the character (`Model::setCharacterInput`).
- In `FreeFly`, WASD moves camera position directly.
- Simulated REST key windows are time-bounded and auto-cleared.

### Single-owner character control
`ViewportHostWidget::setCharacterControlState(...)` enforces one controllable model at a time:
- enabling one character disables others and clears their inputs/velocity flags.

## Character Motion Layer
Character kinematics are updated in:
- `Model::updateCharacterPhysics(...)` (with or without physics backend).

Core behavior:
- Computes target planar velocity from input.
- Blends velocity toward target using acceleration.
- Applies gravity/jump and ground constraints.
- Updates model `worldTransform` translation.
- Rotates character facing toward movement direction with configurable responsiveness.
- Chooses animation state (`Idle`, `ComeToRest`, directional walk/run, `Jump`).

## Animation Preprocessing Layer
Animation update occurs in:
- `Model::updateAnimation(double dt)` -> `motive::animation::updateFbxAnimation(...)`.

In `animation.cpp`:
- FBX scene is evaluated per-frame.
- `evalOpts.evaluate_skinning = true` so CPU-side animated/skinned positions are always available for bounds-dependent systems.
- Primitive CPU vertices are updated from evaluated animation result.
- Model bounds are recomputed from updated geometry.

### Provenance flags (runtime)
Model now tracks preprocessing progression:
- `animationPreprocessedFrameValid`
- `animationPreprocessedFrameCounter`

They are updated when an animation preprocessing frame is successfully produced.

## Bounds, Follow Anchor, And Stabilization
### Bounds recompute
`Model::recomputeBounds()` computes:
- `boundsCenter`, `boundsRadius`
- world AABB min/max

It also captures a one-time stable local center:
- `followAnchorLocalCenter` + `followAnchorLocalCenterInitialized`

### Follow anchor policy
`Model::getFollowAnchorPosition()` applies mode-specific anchor ownership:
- Controllable character + initialized stable center:
  - returns `worldTransform * followAnchorLocalCenter` (`stableLocalAnchor`).
- Otherwise:
  - returns animated `boundsCenter` (`preprocessedAnimatedBounds`).
- Fallback:
  - model translation from `worldTransform[3]`.

Why:
- `preprocessedAnimatedBounds` is accurate for non-controllable assets/framing.
- `stableLocalAnchor` avoids limb-jitter and loop-end anchor snaps for player-follow cameras.

## Camera Follow Layer
### Entry
`Display::updateRuntimeControllers(dt)` iterates cameras and calls:
- `Camera::updateFollow(dt, engine->models)` for follow-enabled cameras.

### Follow target inputs
`Camera::updateFollow(...)`:
- Resolves followed model by scene index.
- Computes raw target center from `Model::getFollowAnchorPosition()` plus target offset.
- Passes raw center through `FollowTargetTracker`.

### Target tracker outputs
`FollowTargetTracker::update(...)` returns:
- `rawCenter`
- `motionCenter` (smoothed center, user-controlled by follow smooth speed)

Current follow solve uses one center consistently for orbit and look-at, preventing phase-error swing.

### Orbit/follow rig
- `CharacterFollowRig` and `OrbitRig` solve camera pose around target.
- Camera view-lock remains hard-centered on the selected follow target.

## REST And UI Exposure
## Endpoints
- `POST /controls/character`: controllable toggle + synthetic WASD/jump injection.
- `POST /controls/camera`: list/create/update/select camera; supports follow params including `smooth`.
- `GET /profile/scene`: returns scene profile + camera tracking diagnostics.

### Scene profile fields (`sceneItems[i]`)
Includes:
- transform/bounds (`boundsCenter`, `boundsMin`, `boundsMax`, etc.)
- character runtime state (keys, inputDir, velocity, grounded, anim state)
- animation runtime state (`runtimeActiveClip`, `runtimeAnimationPlaying`, ...)
- preprocessing provenance:
  - `animationPreprocessedFrameValid`
  - `animationPreprocessedFrameCounter`
- follow anchor provenance:
  - `followAnchorMode` (`stableLocalAnchor` or `preprocessedAnimatedBounds`)
  - `followAnchorReferencesPreprocessedFrames`

### Camera tracking fields (`cameraTracking`)
Includes:
- camera pose/mode/follow target info
- target positions (`targetPos`, `targetPosRaw`, `targetPosMotion`, `targetPosModelAnchor`)
- target projection diagnostics (`targetNdc`, `targetNdcRaw`, warnings)
- anchor provenance for active target:
  - `targetAnchorMode`
  - `targetAnchorReferencesPreprocessedFrames`
  - `targetAnimationPreprocessedFrameValid`
  - `targetAnimationPreprocessedFrameCounter`

## Ownership Summary (Single Source Of Truth)
- Character locomotion state ownership: `Model::character` + `worldTransform`.
- Animation frame ownership: `FbxRuntime` + per-frame evaluated scene in `updateFbxAnimation()`.
- Follow anchor ownership:
  - controllable characters: stabilized local-center anchor transformed by `worldTransform`.
  - non-controllable assets: animation-preprocessed bounds center.
- Camera follow ownership: `Camera::followOrbit` + `FollowTargetTracker` + rig solve.
- External observability ownership: `sceneProfileJson()` and `cameraTrackingDebugJson()` consumed by `/profile/scene`.

## Non-Destructive Guarantee
All stabilization and tracking logic is runtime-only:
- no mutation of source FBX/GLTF assets,
- no rewrite of clip data on disk,
- no destructive preprocessing cache persisted to assets.

