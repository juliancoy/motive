# Motive Engine - C++ File Hierarchy

This document explains the hierarchy and relationships between the core C++ files in the Motive rendering engine.

## Overall Architecture

The Motive engine follows a hierarchical structure where the **Engine** class serves as the central coordinator, managing the **Display** system which in turn handles rendering and contains **Cameras** and **Models** composed of **Meshes** and **Primitives**.

## Core Class Hierarchy

```
Engine (engine.h/engine.cpp)
├── Display (display.h/display.cpp)
│   ├── Camera (camera.h/camera.cpp)
│   └── Models (via Engine's models collection)
│       ├── Model (model.h/model.cpp)
│       │   ├── Mesh (model.h/model.cpp)
│       │   │   └── Primitive (model.h/model.cpp)
│       │   └── Texture (texture.h/texture.cpp)
│       └── Material (texture.h/texture.cpp)
```

## Detailed Class Relationships

### Engine Class
**Files:** `engine.h`, `engine.cpp`

**Role:** Central coordinator and Vulkan context manager

**Responsibilities:**
- Vulkan instance, device, and queue management
- Memory allocation and buffer management
- Descriptor pool and layout management
- Command pool management
- Model lifecycle management
- Display system coordination

**Key Dependencies:**
- Contains `Display* display` pointer
- Manages `std::vector<Model> models`
- Provides Vulkan device context to all subsystems

### Display Class
**Files:** `display.h`, `display.cpp`

**Role:** Window management, rendering pipeline, and camera coordination

**Responsibilities:**
- GLFW window creation and management
- Vulkan swapchain and surface management
- Graphics pipeline creation
- Camera instance management and coordination
- Input event forwarding to cameras
- Frame rendering and synchronization
- Multi-camera viewport management

**Key Dependencies:**
- Contains pointer to `Engine* engine`
- Manages `std::vector<Camera*> cameras`
- Forwards input events to active cameras
- References models through Engine's model collection

### Camera Class [NEW: Separate class]
**Files:** `camera.h`, `camera.cpp`

**Role:** Camera state management, transformations, and input handling

**Responsibilities:**
- Camera position and rotation state management
- View and projection matrix calculations
- Camera UBO creation and management
- Input handling for camera movement (WASD + mouse)
- Camera descriptor set allocation
- Viewport and projection properties

**Key Dependencies:**
- References `Engine* engine` for Vulkan operations
- Contains `CameraTransform` UBO for view/projection matrices
- Manages `descriptorSet` for rendering
- Handles input events forwarded from Display

**Key Components:**
- `cameraPos`, `cameraRotation` state variables
- `cameraTransformUBO` for uniform buffer
- `updateCameraMatrices()` method for matrix updates
- Input handling methods for mouse and keyboard

### Model Class
**Files:** `model.h`, `model.cpp`

**Role:** 3D model container and manager

**Responsibilities:**
- Model data loading (GLTF or manual vertices)
- Mesh collection management
- Resource cleanup

**Key Dependencies:**
- Contains `std::vector<Mesh> meshes`
- Contains `std::vector<Texture*> textures`
- References `Engine* engine` for Vulkan operations

### Mesh Class
**Files:** `model.h`, `model.cpp`

**Role:** Mesh container for primitives

**Responsibilities:**
- Primitive collection management
- Mesh-level transformations

**Key Dependencies:**
- Contains `std::vector<Primitive> primitives`
- References parent `Model* model`

### Primitive Class
**Files:** `model.h`, `model.cpp`

**Role:** Individual renderable geometry unit

**Responsibilities:**
- Vertex buffer management
- Texture resource management
- Uniform buffer management for object transforms
- Descriptor set management

**Key Dependencies:**
- Manages `vertexBuffer`, `vertexBufferMemory`
- Contains `ObjectTransformUBO` for per-object transforms
- Contains `primitiveDescriptorSet` for rendering
- References `Engine* engine` for Vulkan operations

### Texture Class
**Files:** `texture.h`, `texture.cpp`

**Role:** Texture resource management

**Responsibilities:**
- Texture image creation and management
- Sampler creation
- Image view management
- Texture descriptor set updates

**Key Dependencies:**
- Manages `textureImage`, `textureImageView`, `textureSampler`
- References `Mesh* mesh` for association

### Material Class
**Files:** `texture.h`, `texture.cpp`

**Role:** Material properties container

**Responsibilities:**
- Texture collection management
- Material property definitions

**Key Dependencies:**
- Contains `std::vector<Texture*> textures`

## Data Flow

1. **Initialization:**
   - `Engine` creates Vulkan context
   - `Engine` creates `Display` system
   - `Display` creates `Camera` instances
   - `Display` creates window and graphics pipeline
   - Models are added to `Engine` via `addModel()`
   - Cameras allocate descriptor sets after pipeline creation

2. **Input Handling:**
   - GLFW input events captured by `Display`
   - Events forwarded to active `Camera` instances
   - `Camera` handles mouse movement and keyboard input
   - Camera state updated based on input

3. **Rendering Loop:**
   - `Display::render()` called each frame
   - All cameras updated via `camera->update()`
   - For each camera: set viewport/scissor and bind descriptor set
   - For each model: meshes → primitives rendered
   - Each primitive binds its descriptor sets and draws

4. **Multi-Camera Support:**
   - Multiple cameras can be active simultaneously
   - Each camera has its own viewport and projection
   - Scene rendered once per camera with different transforms

## Resource Management

- **Engine:** Owns Vulkan device, descriptor pool, command pool
- **Display:** Owns swapchain, pipeline, window resources
- **Camera:** Owns camera UBO, descriptor set, camera state
- **Model:** Owns meshes and textures
- **Primitive:** Owns vertex buffers, object UBOs, descriptor sets
- **Texture:** Owns image resources and samplers

## Key Design Patterns

- **Composition over Inheritance:** Complex objects built from simpler components
- **Resource Ownership:** Each class manages its own Vulkan resources
- **Dependency Injection:** Classes receive `Engine*` for Vulkan operations
- **Separation of Concerns:** Clear division between rendering, camera management, model management, and resource management
- **Event Forwarding:** Display forwards input events to Camera instances

## Memory Management

- **Engine:** Owns Vulkan device, descriptor pool, command pool
- **Display:** Owns swapchain, pipeline, window resources
- **Camera:** Owns camera UBO, descriptor set
- **Model:** Owns meshes and textures
- **Primitive:** Owns vertex buffers, object UBOs, descriptor sets
- **Texture:** Owns image resources and samplers

This hierarchical structure allows for efficient resource management and clear separation of responsibilities while maintaining flexibility for future extensions. The separation of Camera into its own class enables better organization and potential multi-camera scenarios.

## Directive: GPU Offscreen Blit Encode
- Add a GPU-only offscreen render path and FFmpeg-based Vulkan encode that mirrors the existing Vulkan decode path; avoid CPU readbacks.
- Build FFmpeg’s encoder hwcontext on the existing Vulkan instance/device, match source resolution/frame rate/colorspace, and write `<input>_blit.mp4`.
- Introduce a CLI `--encode` mode that drives the offscreen render/encode pipeline without windows.

## In-Progress Plan: Offscreen Encode Integration
- Build an offscreen render target (Vulkan image) and reuse the video blit compute to render into it without a swapchain, keeping grading/crop applied.
- Wire an FFmpeg Vulkan encoder pipeline that consumes the offscreen image (shared VkImages/semaphores), matching source properties, outputting `<input>_blit.mp4`.
- Add CLI gating (`--encode`, `--windows=...`) to run headless encode; fall back gracefully if FFmpeg encode init fails.
