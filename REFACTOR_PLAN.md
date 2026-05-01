# Refactor Plan for Files Over 1500 LOC

## Scope

Files currently over 1500 lines:

1. `host_widget.cpp` (3938)
2. `shell.cpp` (2888)
3. `video.cpp` (1839)

## Objectives

1. Reduce each oversized translation unit below ~900 LOC.
2. Preserve behavior and runtime UX/API contracts.
3. Land changes in small, testable, reviewable slices.
4. Improve module boundaries to reduce future file growth.

## Phase 0: Guardrails (1 day)

### Tasks

1. Freeze current behavior with tests before structural changes.
2. Add/expand coverage around:
   - TPS bootstrap and `/controls/bootstrap_tps`
   - `/controls/selection` and hierarchy synchronization
   - Camera mode transitions (follow/free-fly)
   - Video decode lifecycle and frame upload path
3. Add a CI warning check (non-blocking initially) for `.cpp` files over 1500 LOC.

### Exit Criteria

1. Baseline tests pass before any extraction work starts.
2. Developers get visible feedback when files exceed threshold.

## Phase 1: Decompose `host_widget.cpp` (highest risk)

### Proposed File Breakdown

1. `host_widget_scene_io.cpp`
   - `loadAssetFromPath`
   - `loadSceneFromItems`
   - `addAssetToScene`
   - Scene change notifications
2. `host_widget_camera.cpp`
   - Camera config CRUD
   - Active/focused camera management
   - Follow camera create/ensure/delete
3. `host_widget_character.cpp`
   - Character control ownership
   - WASD input injection and patterns
   - TPS bootstrap/report/state
4. `host_widget_motion_debug.cpp`
   - Motion history capture
   - Motion summary generation
   - Overlay option serialization/application
5. `host_widget_render_loop.cpp`
   - `renderFrame`
   - Render timer orchestration
   - Per-frame update sequencing

### Constraints

1. Move code first; avoid behavioral rewrites in same commit.
2. Keep public header contracts stable during extraction.
3. Use internal helpers/anonymous namespace for private implementation details.

### Exit Criteria

1. `host_widget.cpp` reduced to orchestration-focused file (<700 LOC target).
2. UX/runtime tests pass unchanged.

## Phase 2: Decompose `shell.cpp`

### Proposed File Breakdown

1. `shell_layout.cpp`
   - Widget creation
   - Layout composition
   - Tab/splitter assembly
2. `shell_hierarchy.cpp`
   - Hierarchy population
   - Selection and context menu actions
   - Deterministic resync/retry logic
3. `shell_inspector.cpp`
   - Inspector population
   - Inspector update handlers
   - Transform/animation/physics control bindings
4. `shell_camera_controls.cpp`
   - Camera settings panel wiring
   - Follow controls
   - WASD routing and free-fly controls
5. `shell_debug_json.cpp`
   - `uiDebugJson`
   - `inspectorDebugJson`
   - Widget metrics serialization

### Exit Criteria

1. `shell.cpp` reduced to top-level window lifecycle and bootstrap wiring.
2. Manual smoke + automated UX tests pass.

## Phase 3: Decompose `video.cpp`

### Proposed File Breakdown

1. `video_demux_decode.cpp`
   - Packet read/demux
   - Software decode path
   - Codec init/teardown
2. `video_hwaccel.cpp`
   - Hardware acceleration probing and setup
   - Fallback decisions
3. `video_frame_queue.cpp`
   - Frame buffering and queue state
   - Flush/reset during seek/restart
4. `video_upload.cpp`
   - GPU upload and conversion staging
   - Texture handoff integration
5. `video_sync.cpp`
   - Playback clock
   - Frame scheduling/drift control

### Exit Criteria

1. `video.cpp` reduced to facade/lifecycle coordination.
2. Seek/EOS/restart/decode fallback behavior remains stable under tests.

## Cross-Cutting Engineering Rules

1. Keep refactor commits behavior-preserving whenever possible.
2. Avoid mixed concerns in one PR (structure-only PRs first).
3. Preserve endpoint contracts unless explicitly versioned/documented.
4. Add missing targeted tests when extracting risky logic.
5. Ensure includes and ownership boundaries are explicit (no hidden coupling).
6. Prefer real translation units (`.cpp`) over implementation includes (`.inc`) for long-term maintainability.
7. Follow Google C++ Style Guide priorities: readability, clear ownership boundaries, small focused files, and stable APIs.

## Recovery Lessons (Applied)

1. Large one-shot cross-file splits are too risky in this codebase due to hidden coupling across helpers and anonymous namespace internals.
2. Safe approach is incremental:
   - Extract one subsystem at a time.
   - Compile and run UX tests after each extraction.
   - Avoid simultaneously moving constructors, helper namespaces, and method bodies across translation units.
3. Decomposition must start with low-coupling surfaces (`shell` helpers/methods), then medium (`video`), then high-coupling (`host_widget`).
4. Use temporary guardrails before hard CI failure:
   - Warn-only size checks first.
   - Enforce (`--fail`) only after scoped first-party coverage is stable.

## Immediate Next Steps (Proceeding)

1. Add scoped first-party C++ file-size checker (exclude third-party/vendor trees).
2. Wire checker into `build.sh` as warn-only by default.
3. Start incremental split sequence:
   - Step A: `shell.cpp` split into `shell_runtime.cpp` + `shell.cpp` with stable helper ownership.
   - Step B: `video.cpp` split into decode/runtime and overlay/playback units.
   - Step C: `host_widget.cpp` split by domain with explicit shared internal headers for cross-unit helpers.
4. After each step:
   - `./build.sh --test-ux`
   - Endpoint smoke (`/health`, `/profile/tps_state`, `/controls/bootstrap_tps`, `/controls/selection`).

## Suggested PR Sequence

1. PR1: Guardrails and baseline test hardening.
2. PR2-PR4: `host_widget` extraction in 3 slices.
3. PR5-PR7: `shell` extraction in 3 slices.
4. PR8-PR9: `video` extraction in 2 slices.
5. PR10: Cleanup pass + enforce blocking LOC threshold.

## Validation Per PR

1. `./build.sh --test-ux`
2. Editor control-server smoke checks:
   - `/health`
   - `/profile/scene_state`
   - `/profile/tps_state`
   - `/controls/bootstrap_tps`
   - `/controls/selection`
3. If touching video paths, run targeted runtime playback/seek smoke.

## Definition of Done

1. No scoped `.cpp` file remains over 1500 LOC.
2. Test suite and runtime UX checks pass consistently.
3. Architecture boundaries are clear and documented in code layout.
4. New growth pressure is contained by CI line-count enforcement.
