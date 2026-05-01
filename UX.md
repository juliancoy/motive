# UX Flows and Improvement Opportunities

## Purpose
This document describes how users move through Motive’s core programs today and where the experience can be improved.

Scope:
- `motive3d` (runtime renderer)
- `motive2d` (video player)
- `motive_editor` (Qt6 editor + REST control)
- `encode` (headless codec tool)

## User Profiles
- Technical artist: imports models/scenes, validates look and animation.
- Graphics/engine developer: tests rendering, physics, and performance.
- Video pipeline developer: validates decode/overlay/encode behavior.
- Tooling/integration developer: automates editor control through REST.

## High-Level Product Journey
1. Install dependencies and build.
2. Launch one of the executables by task.
3. Load content (model/video/project).
4. Inspect and iterate (camera, overlays, animation, physics).
5. Save/share outputs (project files, captures, encoded output).

Primary friction today: users choose the correct executable and mode mostly from memory/CLI knowledge rather than guided in-app workflows.

## Flow 1: First-Time Setup and First Frame
### Current flow
1. User installs system dependencies.
2. User runs `python build_deps.py`.
3. User builds (`./build.sh` or `python build.py`).
4. User launches `./motive3d`.
5. User infers current mode (video grid by default) from visual output.

### Pain points
- Setup success criteria are not explicit (what “good” looks like after dependency build).
- First launch mode may not match user intent (3D model testing vs video grid).
- Missing “quick health check” that confirms Vulkan/device/queue capabilities in plain language.

### Improvements
- Add a first-run diagnostics summary (`--doctor` and optional startup panel):
  - Vulkan version, validation layer status, queue families, FFmpeg/NCNN/Bullet availability.
  - Pass/fail with actionable fixes.
- Add `--welcome` guided start mode:
  - “Open model”, “Play video”, “Open project”, “Run benchmark scene”.
- Print explicit post-build checklist in `build.sh` output:
  - Which executables were produced.
  - Which optional features are disabled due to missing deps.

## Flow 2: 3D Runtime (`motive3d`) Model Exploration
### Current flow
1. User runs `./motive3d --gltf` or `./motive3d path/to/model.glb`.
2. Engine loads model and renders with forward pipeline.
3. User explores scene via camera controls.
4. User may toggle debug/physics features via available UI paths.

### Pain points
- Entry mode flags are discoverable only via external knowledge.
- Camera controls and follow-mode behavior may be unclear without prompt/help overlay.
- Multi-camera capability is powerful but likely under-discovered.

### Improvements
- Add an in-app “controls/help” overlay toggle (`F1`):
  - Camera bindings, mode, follow target, current render stats.
- Add “content bootstrap” panel:
  - Recent models/projects.
  - Drag-and-drop support hints.
- Introduce a lightweight “scene inspector HUD”:
  - Active camera, primitive count, animation state, physics body count.

## Flow 3: Physics Onboarding and Interaction
### Current flow
1. Developer enables physics bodies in code or project setup.
2. Main loop steps physics and syncs transforms.
3. User observes rigid body behavior visually.

### Pain points
- Physics is available but likely invisible as a user-facing feature in runtime UX.
- No obvious affordance for collision debugging/raycast interaction during scene inspection.

### Improvements
- Add runtime physics debug panel:
  - Backend in use, simulation rate, body count, sleeping bodies.
- Add optional gizmo overlays:
  - Collider wireframes, contact points, COM markers.
- Add click-to-raycast inspector:
  - On click, display hit object, distance, material/friction/restitution.

## Flow 4: 2D Video Playback (`motive2d`)
### Current flow
1. User runs `./motive2d [video.mkv]`.
2. Video decode starts (software path; hw path WIP).
3. Compute overlays/scrubber may be shown.
4. User evaluates playback and decode behavior.

### Pain points
- Playback status (decoder path, dropped frames, latency) may not be transparent.
- Overlay debugging tools appear developer-centric rather than task-oriented.
- Seeking/scrubbing confidence depends on visual feedback quality.

### Improvements
- Add persistent playback telemetry strip:
  - Decode path, FPS, dropped frames, queue depth, A/V sync delta.
- Add user-facing playback states:
  - Buffering, stalled decode, recoverable errors.
- Improve scrub UX:
  - Thumbnail previews, frame-step controls, keyframe markers.

## Flow 5: Editor (`motive_editor`) Project Authoring
### Current flow
1. User launches `./motive_editor [project_dir]`.
2. Editor loads default or specified project.
3. User manipulates scene hierarchy, camera, and assets.
4. External tools may call REST API on port `40132`.

### Pain points
- Port-based API integration is powerful but not discoverable inside editor.
- Scene hierarchy + viewport workflows may lack explicit “task lanes” (layout/edit/preview/play).
- Errors in project serialization/loading can feel opaque without strong diagnostics.

### Improvements
- Add onboarding sidebar with “common tasks”:
  - Import model, add light, add camera, enable physics, save project.
- Add integrated API console:
  - Show health, recent API calls, copyable example requests.
- Add project validation panel before save/load:
  - Broken asset references, unsupported material/shader paths, missing animations.

## Flow 6: Headless Encode/Decode Validation (`encode`)
### Current flow
1. User runs `./encode` with expected input stream files.
2. Pipeline processes H.264/H.265 paths.
3. User inspects console logs/artifacts for success.

### Pain points
- File expectations and output locations are implicit.
- Success/failure often represented in low-level logs.
- Hard to compare runs for regressions.

### Improvements
- Add explicit CLI contract:
  - `--input`, `--output`, `--codec`, `--report`.
- Emit machine-readable summary (`json`):
  - Frames processed, throughput, error count, fallback usage.
- Add baseline comparison mode:
  - Compare metrics against prior run and highlight regressions.

## Cross-Flow UX Opportunities
### 1) Unify launch and mode selection
A small launcher (CLI TUI or editor-start dialog) would reduce cognitive load:
- “3D scene”, “2D playback”, “Editor”, “Headless encode”.
- Shows environment readiness for each mode.

### 2) Improve discoverability
- Standardize `--help` across all binaries with examples.
- Add in-app command palette for major actions.
- Provide recent files/projects everywhere.

### 3) Better error communication
- Translate low-level subsystem failures into user-actionable messages.
- Include “what failed”, “impact”, “how to fix”, “continue with fallback?”.

### 4) Consistent observability
- Shared performance/debug HUD conventions across apps.
- Consistent terminology: frame time, decode path, validation status, backend.

### 5) Faster iteration loops
- Hot-reload where feasible (shaders/projects).
- “Reload last scene/video” shortcut.
- One-click capture bundle for bug reports (logs + settings + hardware snapshot).

## Prioritized Improvement Backlog (Speculative)
1. Add unified `--help` and first-run diagnostics (`--doctor`) across binaries.
2. Add in-app help overlay (`F1`) with controls/status in `motive3d` and `motive2d`.
3. Add editor onboarding sidebar + project validation panel.
4. Add structured telemetry/report outputs for `motive2d` and `encode`.
5. Add physics visualization and click-raycast inspector.
6. Add launcher/startup task chooser.

Rationale:
- Items 1-3 improve immediate usability for every user profile.
- Items 4-5 improve debugging confidence and reduce time-to-fix.
- Item 6 is high leverage but can follow once core affordances are in place.

## Suggested UX Success Metrics
- Time to first successful render/playback/project load.
- % of sessions with unrecovered startup errors.
- Task completion time:
  - Import model and save project.
  - Load video and scrub to target timestamp.
  - Run encode and export report.
- User reliance on external docs for basic controls.
- Regression detection latency for video/physics/rendering changes.

## Implementation Notes
- Keep this incremental; avoid large redesigns before instrumentation.
- Prefer additive UX layers (diagnostics, overlays, panels) over disruptive workflow changes.
- Validate with 3-5 representative tasks per user profile before deeper UI refactors.
