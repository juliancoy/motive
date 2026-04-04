# Qt Engine Editor Blueprint

## Goal

Move `motive3d/` from a GLFW-first demo/runtime into a Qt-hosted engine editor with:

- the same general shell structure as `editor/`
- the same file selector and gallery behavior
- a shared viewport interaction model
- a clean separation between engine runtime and editor UI

## Source Material

The active implementation lives directly in the `*` modules. Legacy copied reference files have been purged.

## Target Modules

### 1. Shell

Public headers:

- `main_window_shell.h`
- `dock_layout_config.h`

Implementation:

- `main_window_shell.cpp`
- `main_window_actions.cpp`
- `main_window_layout.cpp`
- `main_window_menus.cpp`

Responsibilities:

- create the `QMainWindow`
- own dock areas and splitters
- own menu/toolbar/action wiring
- stay independent from rendering internals

### 2. Browser

Public headers:

- `asset_browser_widget.h`
- `asset_browser_types.h`
- `thumbnail_provider.h`

Implementation:

- `asset_browser_widget.cpp`
- `asset_browser_tree.cpp`
- `asset_browser_gallery.cpp`
- `asset_browser_thumbnail_worker.cpp`
- `media_thumbnail_provider.cpp`

Responsibilities:

- filesystem tree
- gallery grid
- hover preview
- file activation callbacks
- thumbnail generation and caching

Current implementation:

- `asset_browser_widget.*`

### 3. Viewport

Public headers:

- `viewport_host_widget.h`
- `viewport_scene_bridge.h`
- `viewport_interaction_controller.h`

Implementation:

- `viewport_host_widget.cpp`
- `viewport_overlay_painter.cpp`
- `viewport_interaction_controller.cpp`
- `qt_vulkan_viewport_bridge.cpp`

Responsibilities:

- host the engine renderer inside Qt
- translate Qt input events into engine/editor commands
- draw shared overlays and selection chrome
- avoid editor timeline dependencies

Current implementation:

- `viewport_host_widget.*`
- rendering backend adapted from `motive3d/display.*`

### 4. Project

Public headers:

- `project_state_store.h`
- `project_session.h`
- `recent_projects_model.h`

Implementation:

- `project_state_store.cpp`
- `project_session.cpp`
- `recent_projects_model.cpp`

Responsibilities:

- current project tracking
- recent-project list
- per-project UI state
- asset browser root persistence

Current implementation:

- `project_session.*`

### 5. App Bootstrap

Public headers:

- `motive_editor_app.h`

Implementation:

- `motive_editor_app.cpp`
- `motive_editor_main.cpp`

Responsibilities:

- initialize `QApplication`
- build the shell
- connect shell, browser, viewport, and project session

## Runtime Boundary

The engine-side runtime should remain outside this tree.

Expected runtime boundary:

- `motive3d/engine.*`
- `motive3d/display.*`
- `motive3d/model.*`
- `motive3d/video.*`
- future `motive3d/scene/*`

Required change:

`Display` must stop owning the native app window directly. It should expose a
render-surface adapter that a Qt viewport host can own.

## File Split Constraints

Hard rule: keep all new files under `2000` lines, with a practical target of
`400-800` lines for implementation files and much smaller for headers.

Recommended split triggers:

- new widget page: separate file
- separate worker thread: separate file
- separate persistence concern: separate file
- overlay drawing and input handling: separate files

## Recommended Build Sequence

1. Add Qt bootstrap files at the `motive3d/` root with the `` prefix.
2. Implement the shell layout with placeholder widgets.
3. Introduce viewport bridge interfaces without changing runtime rendering yet.
4. Adapt `motive3d/display.*` into a Qt-owned viewport surface.
5. Expand project persistence and recent-project handling as needed.
6. Replace placeholder widgets one subsystem at a time.
