# Motive3D Brain Viewer

**C++ Vulkan implementation** with integrated Dear ImGui file selector for side-by-side video and brain visualization.

## Architecture

```
┌──────────────────────────────────────────────────────┐
│  C++ Engine (Vulkan + ImGui)                         │
│  ┌──────────────────┬──────────────────────────────┐ │
│  │  File Selector   │  Main Viewer                 │ │
│  │  (ImGui Window)  │  ┌────────────┬────────────┐ │ │
│  │                  │  │   VIDEO    │   BRAIN    │ │ │
│  │  📁 Dir Tree     │  │  (Left)    │  (Right)   │ │ │
│  │  📄 File Pairs   │  │            │   (3D)     │ │ │
│  │  [Open Selected] │  └────────────┴────────────┘ │ │
│  └──────────────────┴──────────────────────────────┘ │
└──────────────────────────────────────────────────────┘
                    │
         Python Wrapper (ctypes)
                    │
         User Scripts/API
```

## Files

| File | Language | Purpose |
|------|----------|---------|
| `brain_viewer_engine.cpp` | C++ | Main Vulkan renderer + ImGui file selector |
| `brain_viewer_engine.h` | C | C API header |
| `file_selector_cpp.h/cpp` | C++ | Dear ImGui file browser |
| `brain_viewer.py` | Python | Python wrapper |

## Build

```bash
cd motive3d
python build.py  # Compiles all .cpp files into libengine.so
```

### Dependencies
- Vulkan 1.2+
- GLFW3
- Dear ImGui (included in tinygltf/examples/common/imgui)
- FFmpeg

## Usage

### With File Selector (Default)
```bash
python brain_viewer.py
```
Opens an ImGui file browser to select video + brain data pairs.

### Direct File Loading
```bash
python brain_viewer.py --no-selector --video video.mp4 --npy video_tribe.npy
```

### Python API with File Selector
```python
from brain_viewer import BrainViewer

viewer = BrainViewer()
viewer.show_file_selector()  # Show C++ ImGui file selector
viewer.run()  # Main loop handles file selection automatically

# Or check selection manually
if viewer.has_file_selection():
    video, npy = viewer.get_selected_files()
    print(f"Selected: {video}, {npy}")
```

## File Selector UI

The C++ ImGui file selector provides:

### Layout
```
+----------------------------------------+
| File |                  |              |
+----------------------------------------+
| Directories      |  Video + Brain Data |
|                  |  Pairs              |
| 🏠 Home          |                     |
| 🖥️ Desktop       |  Status │ Video │   |
| 📁 Documents     |  ─────────────────  |
| 🎬 Videos        |  ✓ Complete │ vid.. │
|                  |  ⚠ No NPY   │ vid.. │
| 📁 current/      |  ✗ No Video │  -    │
|   📁 subdir/     |                     |
|                  |  [Open Selected]    |
+----------------------------------------+
| /path/to/data | 5 complete / 8 total  |
+----------------------------------------+
```

### Features
- **Directory Tree**: Quick access to Home, Desktop, Documents, Videos
- **Navigation**: Browse subdirectories, back button
- **Auto-pairing**: Matches `video.mp4` → `video_tribe.npy`
- **Filter**: Search box to filter pairs
- **Status Icons**: 
  - ✓ Green = Complete pair
  - ⚠ Orange = Missing NPY
  - ✗ Red = Missing video

### Naming Convention
The file selector automatically pairs files based on naming:
```
subject01_session1.mp4  →  subject01_session1_tribe.npy
video.mp4               →  video_tribe.npy
```

## Controls

### File Selector
| Input | Action |
|-------|--------|
| Click row | Select pair |
| Double-click | Open selected pair |
| Filter box | Search/filter pairs |
| Dir tree | Navigate directories |

### Main Viewer
| Key | Action |
|-----|--------|
| `SPACE` | Play/Pause |
| `←` / `→` | Scrub backward/forward |
| `Q` / `ESC` | Show file selector / Quit |
| Mouse | Rotate brain (right side) |

## C++ API

### File Selector (C++)
```cpp
#include "file_selector_cpp.h"

motive::FileSelectorUI selector;
selector.setDirectory("/path/to/data");
selector.setCallback([](const motive::FilePair& pair) {
    std::cout << "Selected: " << pair.videoPath << std::endl;
});

// In render loop
selector.draw();  // Draws ImGui window
```

### C API with File Selector
```c
BrainViewerHandle viewer = brain_viewer_create(1920, 1080);

// Show file selector
brain_viewer_show_file_selector(viewer, 1);

// In render loop
while (brain_viewer_render_frame(viewer)) {
    if (brain_viewer_has_file_selection(viewer)) {
        char video[1024], npy[1024];
        brain_viewer_get_selected_video_path(viewer, video, 1024);
        brain_viewer_get_selected_npy_path(viewer, npy, 1024);
        
        // Load and hide selector
        brain_viewer_load_video(viewer, video);
        brain_viewer_show_file_selector(viewer, 0);
        brain_viewer_play(viewer);
    }
}
```

## Implementation Notes

### ImGui Integration
The file selector uses Dear ImGui rendered on top of the Vulkan viewport:
1. `ImGui::NewFrame()` at start of render loop
2. `fileSelector.draw()` renders the UI
3. `ImGui::Render()` + `ImGui_ImplVulkan_RenderDrawData()` to output

### File Pairing Algorithm
```cpp
// Scan directory
for each file:
    if video extension: videos[stem] = path
    if .npy extension:  brains[stem_without_tribe] = path

// Match pairs
for each unique stem:
    pair.video = videos[stem]
    pair.brain = brains[stem]
```

### Future Enhancements
- [ ] Native OS file dialog option
- [ ] Thumbnail previews for videos
- [ ] Recent files list
- [ ] Bookmark favorite directories
- [ ] Recursive directory search
