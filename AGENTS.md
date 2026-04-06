# Motive 3D Engine - AI Agent Guide

## Project Overview

Motive is a C++ 3D rendering engine built on Vulkan with the following capabilities:
- **3D Rendering**: Vulkan-based forward renderer with MSAA, z-culling, and multiple camera support
- **Video Processing**: FFmpeg integration for video decode/encode with Vulkan Video acceleration (WIP)
- **Animation**: GLTF/FBX model loading with skeletal animation and skinning
- **Editor**: Qt6-based editor with REST API control server
- **2D Mode**: Dedicated 2D video player with compute shader overlays
- **Neural Network**: NCNN integration for YOLO pose detection

The engine is designed for interactive 3D applications, video processing pipelines, and game development.

## Technology Stack

| Component | Technology | Purpose |
|-----------|------------|---------|
| Graphics API | Vulkan 1.3+ | GPU rendering and compute |
| Windowing | GLFW 3 | Cross-platform window/input |
| Math | GLM | Vector/matrix operations |
| Models | tinygltf + ufbx | GLTF/GLB and FBX loading |
| Video | FFmpeg 6.0+ | Decode/encode with hwaccel |
| Vulkan Video | Khronos samples | Hardware video decode/encode |
| UI | Qt6 + Dear ImGui | Editor and debug UI |
| Text | FreeType | Font rendering |
| ML Inference | NCNN | YOLO pose detection |
| Physics | Bullet3 | Rigid body dynamics |
| Build | Python 3 + CMake | Dual build system |

## Repository Structure

```
.
├── *.cpp/*.h              # Core engine source files
├── shaders/               # GLSL shaders (compiled to .spv)
├── projects/              # Saved project files (.json)
├── models/                # YOLO model files (.pt, .bin, .param)
├── thirdpersonshooter/    # Sample assets (character, city, jet, arena)
├── LandReform/            # GIS data (Maryland property data)
│
├── glfw/                  # Submodule: windowing library
├── glm/                   # Submodule: math library
├── tinygltf/              # Submodule: GLTF loading
├── ufbx/                  # Submodule: FBX loading (single C file)
├── Vulkan-Headers/        # Submodule: Vulkan headers
├── freetype/              # Cloned: font rendering
├── FFmpeg/                # Cloned: video codec library
├── ncnn/                  # Submodule: neural network inference
├── bullet3/               # Submodule: physics engine (partially integrated)
├── Vulkan-Video-Samples/  # Submodule: Khronos video decoder
├── common_vv/             # Vulkan Video common utilities
├── vk_video_decoder/      # Vulkan Video decoder implementation
├── engine_ui_shared/      # Shared UI components
├── renderdoc_1.39/        # RenderDoc for debugging
└── imgui/                 # Dear ImGui headers
```

## Build System

The project supports two build systems:

### Python Build (Primary)
```bash
# Install dependencies (one-time)
python build_deps.py

# Build engine and all executables
python build.py

# Force full rebuild
python build.py --rebuild
```

**Build outputs:**
- `libengine.a` - Static engine library
- `motive3d` - 3D application executable
- `motive2d` - 2D video player executable
- `encode` - Video encoding/headless Vulkan Video test
- `motive_editor` - Qt6-based editor (if Qt6 available)
- `*.o` - Object files

### Bullet Physics Build
The Bullet3 submodule is built as part of dependency setup:
```bash
python build_deps.py  # Builds Bullet Physics library
```

**Bullet libraries built:**
- `libBulletDynamics.a` - Rigid body dynamics
- `libBulletCollision.a` - Collision detection
- `libLinearMath.a` - Math utilities

### CMake Build (Alternative)
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

**CMake options:**
- `MOTIVE_ENABLE_ASAN=ON` - Enable AddressSanitizer
- `MOTIVE_ENABLE_VALIDATION=ON` - Enable Vulkan validation layers (default: ON)

## Core Architecture

### Class Hierarchy

```
Engine (engine.h/cpp)
├── RenderDevice (graphicsdevice.h/cpp) - Vulkan device management
├── Display (display.h/cpp) - Window, swapchain, rendering
│   ├── Camera (camera.h/cpp) - View/projection, input handling
│   └── Multiple pipelines (standard, skinned, transparent)
├── Model (model.h/cpp) - 3D model container
│   └── Mesh (model.h/cpp) - Primitive collection
│       └── Primitive (primitive.h/cpp) - Renderable geometry
│           ├── Vertex buffers
│           └── Descriptor sets
├── Texture (texture.h/cpp) - Image resources
├── Light (light.h/cpp) - Lighting UBO
├── Animation (animation.h/cpp) - Skeletal animation
└── PhysicsWorld (physics_world.h/cpp) - Bullet physics simulation
    └── PhysicsBody - Rigid body attached to models
```

### Key Design Patterns

1. **Resource Ownership**: Each class manages its own Vulkan resources (RAII)
2. **Descriptor Management**: Centralized descriptor pool in Engine, per-object sets in Primitives
3. **Multi-Camera Support**: Display manages multiple Camera instances with viewport splitting
4. **Batch Uploads**: Engine provides beginBatchUpload()/endBatchUpload() for staging buffer optimization

## Main Executables

### motive3d
3D renderer with video textures and instanced rendering.
```bash
./motive3d                    # Default: video grid mode
./motive3d --gltf             # Load GLTF model mode
./motive3d [path/to/model.glb] # Load specific model
```

### motive2d
2D video player with scrubber and overlays.
```bash
./motive2d [video.mkv]        # Play video file
./motive2d --debugDecode      # Enable decode debugging
```

### encode
Headless Vulkan Video decoder/encoder test.
```bash
./encode                      # Process input.h264/input.h265
```

### motive_editor
Qt6-based editor with REST API.
```bash
./motive_editor [project_dir] # Open project (default: projects/default)
```

**Editor REST API endpoints:** (default port 5050)
- `GET /health` - Server status
- `GET /hierarchy` - Scene hierarchy
- `GET /profile/scene` - Camera position, FPS, etc.
- `POST /controls/camera` - Camera control commands

## Key Source Files

| File | Purpose |
|------|---------|
| `engine.cpp` | Vulkan context, memory management, buffer creation |
| `graphicsdevice.cpp` | RenderDevice class - instance/device/queues |
| `display.cpp` | Swapchain, render pass, pipeline creation |
| `camera.cpp` | Camera transforms, input handling, follow mode |
| `model.cpp` | GLTF/FBX loading, mesh consolidation |
| `primitive.cpp` | Vertex buffers, descriptor sets, instancing |
| `video.cpp` | FFmpeg decoder integration |
| `mini_decoder*.cpp` | Vulkan Video decode (headless path) |
| `mini_encoder*.cpp` | Vulkan Video encode (headless path) |
| `overlay.cpp` | Compute shader overlays for 2D mode |
| `control_server.cpp` | REST API server for editor |
| `motive_editor_app.cpp` | Qt6 editor application |
| `viewport_*.cpp` | Editor viewport components |
| `file_selector_cpp.cpp` | ImGui file browser |
| `physics_world.cpp` | Bullet Physics integration |

## Video Pipeline

### Software Decode Path (FFmpeg)
```
Video file → FFmpeg decoder → CPU YUV frames → Upload to GPU → Render
```

### Hardware Decode Path (Vulkan Video - WIP)
```
Annex-B stream → vkCmdDecodeVideoKHR → DPB images → Blit to RGBA → Render/Encode
```

**Key files:**
- `video.h/cpp` - FFmpeg integration
- `mini_decoder.h/cpp` - Vulkan Video entry points
- `mini_decoder_session.cpp` - VkVideoSession management
- `mini_decode_pipeline.cpp` - Command buffer recording
- `annexb_demuxer.cpp` - Raw H.264/H.265 parsing

## Shader Pipeline

Shaders are compiled from GLSL to SPIR-V using `glslangValidator`:

```bash
# Automatic during build.py
glslangValidator -V shader.vert -o shader.vert.spv
glslangValidator -V shader.frag -o shader.frag.spv
glslangValidator -V shader.comp -o shader.comp.spv
```

**Shader files:**
- `mainforward.vert/frag` - Standard forward rendering
- `mainforward_skinned.vert` - Skeletal animation variant
- `flat2d.vert/frag` - 2D video rendering
- `video_blit.comp` - YUV to RGB conversion
- `overlay_*.comp` - Compute overlays (rect, pose, YOLO)
- `scrubber.comp` - Video scrubber UI
- `rgba_to_nv12.comp` - RGB to NV12 conversion

## Testing

### Test Scripts
```bash
# Test follow camera API (requires running editor)
python test_follow_cam.py [port]

# Brain viewer (video + brain data visualization)
python brain_viewer.py

# Line counting utility
python countlines.py
```

### Test Files
- `test_follow_cam.py` - REST API tests for camera follow mode
- `test_follow_cam.sh` - Shell wrapper for follow cam test

## Development Conventions

### Code Style
- **Language**: C++17
- **Indentation**: 4 spaces (no tabs)
- **Braces**: Allman style (opening brace on new line)
- **Naming**:
  - Classes: `PascalCase` (e.g., `RenderDevice`)
  - Functions: `camelCase` (e.g., `createBuffer`)
  - Member variables: snake_case or m_camelCase
  - Constants: `kCamelCase` or `UPPER_SNAKE`
  - Private members: often no prefix, context-dependent

### Include Order
```cpp
// 1. Standard library
#include <vector>
#include <memory>

// 2. Third-party (Vulkan, GLFW, GLM)
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

// 3. Project headers
#include "engine.h"
#include "display.h"
```

### Vulkan Error Handling
```cpp
// Check results explicitly
if (vkCreateBuffer(device, &info, nullptr, &buffer) != VK_SUCCESS) {
    throw std::runtime_error("Failed to create buffer");
}

// Or use validation layers during development
```

### Memory Management
- Use `std::unique_ptr` for owned resources
- Vulkan handles use manual cleanup in destructors
- Staging buffers use deferred destruction with `deferStagingBufferDestruction()`

## Dependencies Setup

```bash
# Install system packages (Ubuntu/Debian)
sudo apt-get install -y \
    build-essential cmake git \
    libvulkan-dev vulkan-tools \
    libglfw3-dev \
    qt6-base-dev qt6-base-dev-tools \
    libfreetype6-dev \
    libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev \
    nasm \
    libwayland-dev libxkbcommon-dev wayland-protocols \
    libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev

# Clone and build all submodules
python build_deps.py
```

**build_deps.py components:**
1. `setup_vulkan_headers()` - Updates Vulkan-Headers
2. `setup_glfw()` - Builds GLFW with Wayland/X11 auto-detection
3. `setup_tinygltf()` - Builds static library
4. `setup_glm()` - Header-only, verification only
5. `setup_ffnvcodec()` - NVIDIA codec headers (system install)
6. `setup_ffmpeg()` - Full FFmpeg build with hwaccel detection
7. `setup_freetype()` - Font rendering library

## Project File Format

Projects are saved as JSON in `projects/[name]/[name].json`:
- Scene hierarchy (models, cameras, lights)
- Transform data (position, rotation, scale)
- Camera settings (follow targets, viewport)
- Animation state

## Physics Integration

### Quick Start
```cpp
// In your main loop:
auto lastTime = std::chrono::steady_clock::now();
while (!glfwWindowShouldClose(window)) {
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTime).count();
    lastTime = now;
    
    // Step physics simulation
    engine->updatePhysics(dt);
    engine->syncPhysicsToModels();
    
    // Render
    display->render();
}
```

### Creating Physics Bodies
```cpp
// Box with default mass (1kg)
model->enablePhysics(engine->getPhysicsWorld());

// Static ground plane
motive::PhysicsBodyConfig groundConfig;
groundConfig.shapeType = motive::CollisionShapeType::StaticPlane;
groundConfig.mass = 0; // Static
ground->enablePhysics(engine->getPhysicsWorld(), groundConfig);

// Dynamic sphere
motive::PhysicsBodyConfig sphereConfig;
sphereConfig.shapeType = motive::CollisionShapeType::Sphere;
sphereConfig.mass = 5.0f;
sphereConfig.restitution = 0.8f; // Bouncy
sphere->enablePhysics(engine->getPhysicsWorld(), sphereConfig);
```

### Collision Shapes
- `Box` - Axis-aligned bounding box (default)
- `Sphere` - Bounding sphere
- `Capsule` - Capsule (best for characters)
- `Cylinder` - Cylinder
- `StaticPlane` - Infinite ground plane

### Raycasting
```cpp
auto hit = engine->getPhysicsWorld().raycast(
    glm::vec3(0, 10, 0),  // From
    glm::vec3(0, -10, 0)  // To
);
if (hit.hit) {
    std::cout << "Hit at distance: " << hit.distance << std::endl;
}
```

## NCNN / YOLO Integration

YOLO pose detection models are stored in `models/`:
- `.param` - Network architecture
- `.bin` - Weights
- `_ncnn_model/` - Optimized model directories

Compile with `-DNCNN_AVAILABLE -DNCNN_USE_VULKAN=0` for detection files.

## Debugging

### Vulkan Validation
Enabled by default in debug builds. Disable with:
```cpp
// In CMake or build.py
MOTIVE_ENABLE_VALIDATION=OFF
```

### RenderDoc
Capture files (`.rdc`) can be loaded in RenderDoc for frame analysis.

### Address Sanitizer
```bash
# CMake build
mkdir build-asan && cd build-asan
cmake .. -DMOTIVE_ENABLE_ASAN=ON
make
```

## Common Tasks

### Add a new shader
1. Create `shaders/myshader.{vert,frag,comp}`
2. Run `python build.py` (compiles shaders automatically)
3. Load in code with `engine->createShaderModule()`

### Add a new source file
1. Create `myfeature.cpp/h`
2. Add to `MOTIVE_ENGINE_SOURCES` in `CMakeLists.txt` OR
3. It will be auto-discovered by `build.py` (any `.cpp` in root)

### Integrate with Editor
1. Extend `control_server.cpp` for new API endpoints
2. Add UI components in `viewport_*.cpp` files
3. Update serialization in `project_session.cpp`

## Important Notes

1. **FFmpeg Path**: Custom-built FFmpeg is in `FFmpeg/.build/install/`
2. **Shader Compilation**: Shaders are compiled at build time, not runtime
3. **Video Queue**: Engine requests video decode/encode queues separately from graphics
4. **Follow Camera**: Cameras can follow models by scene index for persistence across reloads
5. **Batch Upload**: Use `beginBatchUpload()` / `endBatchUpload()` when loading multiple models
6. **Character Controller**: Models have built-in `CharacterController` for WASD physics
7. **Bullet Physics**: Physics world is auto-initialized; call `engine->updatePhysics(dt)` in render loop
8. **Physics Bodies**: Create with `model->enablePhysics(engine->getPhysicsWorld(), config)`

## Physics System

The engine supports multiple physics backends through a common abstraction layer.

### Available Backends

| Backend | Status | Description |
|---------|--------|-------------|
| **Bullet** | ✅ Available | Mature, stable CPU physics |
| **Jolt** | ⏳ Stub | Modern, fast, multi-threaded CPU physics |
| **Built-in** | ⏳ Stub | GPU-native compute shader physics |

### Physics Files

| File | Purpose |
|------|---------|
| `physics_interface.h` | Common physics interface (IPhysicsWorld, IPhysicsBody) |
| `physics_factory.h/cpp` | Factory for creating physics worlds |
| `physics_bullet.h/cpp` | Bullet Physics implementation |
| `physics_jolt.h` | Jolt Physics stub/header |
| `physics_builtin.h` | Built-in GPU physics stub/header |
| `physics_settings.h` | ImGui settings panel for physics |

### Using Physics

```cpp
// Create physics body for a model
motive::PhysicsBodyConfig config;
config.shapeType = motive::CollisionShapeType::Box;
config.mass = 1.0f;
model->enablePhysics(*engine->getPhysicsWorld(), config);

// In render loop
engine->updatePhysics(deltaTime);  // Steps simulation + syncs transforms
```

### Switching Physics Engines

```cpp
// Switch to Jolt (if available)
engine->setPhysicsEngine(motive::PhysicsEngineType::Jolt);

// Check available backends
auto backends = motive::PhysicsFactory::getAvailableBackends();
for (auto backend : backends) {
    std::cout << motive::PhysicsFactory::getBackendName(backend) << std::endl;
}
```

### Enabling Jolt Physics

To enable Jolt Physics:

1. Add Jolt as a submodule:
```bash
git submodule add https://github.com/jrouwe/JoltPhysics.git jolt
```

2. Build Jolt:
```bash
cd jolt/Build
./cmake_linux.sh
```

3. Enable in build:
```bash
# Add to build.py flags:
-DMOTIVE_JOLT_AVAILABLE
# Link against Jolt libraries
```

### Enabling Built-in GPU Physics

The built-in physics is experimental and uses Vulkan compute shaders.
To enable, define `MOTIVE_BUILTIN_AVAILABLE` and implement the compute shaders.
