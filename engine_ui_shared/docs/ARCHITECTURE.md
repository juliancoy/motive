# Qt Engine Editor Blueprint

## Goal

Move `motive3d/` from a GLFW-first demo/runtime into a Qt-hosted engine editor with:

- the same general shell structure as `editor/`
- the same file selector and gallery behavior
- a shared viewport interaction model
- a clean separation between engine runtime and editor UI

## Source Material

The active implementation lives directly in the `engine_ui_*` modules. Legacy copied reference files have been purged.

## Target Modules

### 1. Shell

Public headers:

- `engine_ui_main_window_shell.h`
- `engine_ui_dock_layout_config.h`

Implementation:

- `engine_ui_main_window_shell.cpp`
- `engine_ui_main_window_actions.cpp`
- `engine_ui_main_window_layout.cpp`
- `engine_ui_main_window_menus.cpp`

Responsibilities:

- create the `QMainWindow`
- own dock areas and splitters
- own menu/toolbar/action wiring
- stay independent from rendering internals

### 2. Browser

Public headers:

- `engine_ui_asset_browser_widget.h`
- `engine_ui_asset_browser_types.h`
- `engine_ui_thumbnail_provider.h`

Implementation:

- `engine_ui_asset_browser_widget.cpp`
- `engine_ui_asset_browser_tree.cpp`
- `engine_ui_asset_browser_gallery.cpp`
- `engine_ui_asset_browser_thumbnail_worker.cpp`
- `engine_ui_media_thumbnail_provider.cpp`

Responsibilities:

- filesystem tree
- gallery grid
- hover preview
- file activation callbacks
- thumbnail generation and caching

Current implementation:

- `engine_ui_asset_browser_widget.*`

### 3. Viewport

Public headers:

- `engine_ui_viewport_host_widget.h`
- `engine_ui_viewport_scene_bridge.h`
- `engine_ui_viewport_interaction_controller.h`

Implementation:

- `engine_ui_viewport_host_widget.cpp`
- `engine_ui_viewport_overlay_painter.cpp`
- `engine_ui_viewport_interaction_controller.cpp`
- `engine_ui_qt_vulkan_viewport_bridge.cpp`

Responsibilities:

- host the engine renderer inside Qt
- translate Qt input events into engine/editor commands
- draw shared overlays and selection chrome
- avoid editor timeline dependencies

Current implementation:

- `engine_ui_viewport_host_widget.*`
- rendering backend adapted from `motive3d/display.*`

### 4. Project

Public headers:

- `engine_ui_project_state_store.h`
- `engine_ui_project_session.h`
- `engine_ui_recent_projects_model.h`

Implementation:

- `engine_ui_project_state_store.cpp`
- `engine_ui_project_session.cpp`
- `engine_ui_recent_projects_model.cpp`

Responsibilities:

- current project tracking
- recent-project list
- per-project UI state
- asset browser root persistence

Current implementation:

- `engine_ui_project_session.*`

### 5. App Bootstrap

Public headers:

- `engine_ui_motive_editor_app.h`

Implementation:

- `engine_ui_motive_editor_app.cpp`
- `engine_ui_motive_editor_main.cpp`

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

1. Add Qt bootstrap files at the `motive3d/` root with the `engine_ui_` prefix.
2. Implement the shell layout with placeholder widgets.
3. Introduce viewport bridge interfaces without changing runtime rendering yet.
4. Adapt `motive3d/display.*` into a Qt-owned viewport surface.
5. Expand project persistence and recent-project handling as needed.
6. Replace placeholder widgets one subsystem at a time.
