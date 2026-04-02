# Engine UI Shared

This directory is the staging area for the Qt-based shell that will be shared by the `editor/` tooling and the future `motive3d/` engine editor.

## Layout

- `docs/`
  Concrete migration notes and file-responsibility plan.
- `../engine_ui_*.h`
  Public interfaces for the shared Qt shell.
- `../engine_ui_*.cpp`
  `motive3d` Qt-shell implementation placeholders.
- `../editor_ref_*.h`
  Copied editor-side header reference material.
- `../editor_ref_*.cpp`
  Copied editor-side source reference material.

## Rules

- Do not add new monolithic files here. Keep implementation files focused.
- Treat `editor_ref_*` files as read-only reference input.
- Extract behavior into the `engine_ui_*` files instead of editing copied
  reference files in place.
