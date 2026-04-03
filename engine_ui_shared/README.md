# Engine UI Shared

This directory is the staging area for the Qt-based shell that will be shared by the `editor/` tooling and the future `motive3d/` engine editor.

## Layout

- `docs/`
  Concrete architecture notes and file-responsibility plan.
- `../engine_ui_*.h`
  Public interfaces for the shared Qt shell.
- `../engine_ui_*.cpp`
  `motive3d` Qt-shell implementation.

## Rules

- Do not add new monolithic files here. Keep implementation files focused.
- Extend the `engine_ui_*` modules directly instead of growing parallel legacy shells.
