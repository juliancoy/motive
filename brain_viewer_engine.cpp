#include "brain_viewer_engine.h"
#include "engine.h"
#include "model.h"
#include "display.h"
#include "camera.h"
#include "file_selector_cpp.h"

#include <imgui.h>

// Define IMGUI_IMPL_API if not already defined (for compatibility with older imgui)
#ifndef IMGUI_IMPL_API
#define IMGUI_IMPL_API IMGUI_API
#endif

#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>

#include <memory>
#include <vector>
#include <string>
#include <iostream>
#include <cstring>

// Internal brain viewer state
class BrainViewer {
public:
    BrainViewer(int w, int h) : width(w), height(h), currentTime(0.0), duration(0.0), isPlaying(false), showFileSelector(true) {
        engine = std::make_unique<Engine>();
        
        // Create main window with split-screen viewport
        display = engine->createWindow(width, height, "Brain Viewer - TribeV2", false, false);
        
        // Setup ImGui for file selector
        setupImGui();
        
        // Setup file selector callback
        fileSelector.setCallback([this](const motive::FilePair& pair) {
            this->onFilePairSelected(pair);
        });
        
        // Setup split-screen cameras
        setupCameras();
        
        std::cout << "[BrainViewer] Created " << width << "x" << height << std::endl;
    }
    
    ~BrainViewer() {
        // Cleanup happens in Engine destructor
    }
    
    double loadVideo(const char* path) {
        videoPath = path;
        // Video is loaded by the display's video decoder
        // This is a placeholder - actual video loading depends on Display implementation
        duration = 60.0; // Placeholder - should get from video
        std::cout << "[BrainViewer] Loaded video: " << path << std::endl;
        return duration;
    }
    
    int loadGLTF(const char* path) {
        // Load brain model from glTF
        try {
            auto model = std::make_unique<Model>(engine.get(), path);
            
            // Position brain on right side
            model->transform.translation = glm::vec3(50.0f, 0.0f, 0.0f);
            model->transform.scale = glm::vec3(0.5f);
            model->transform.updateMatrix();
            
            brainModel = model.get();
            engine->addModel(std::move(model));
            
            std::cout << "[BrainViewer] Loaded glTF: " << path << std::endl;
            return 0;
        } catch (const std::exception& e) {
            std::cerr << "[BrainViewer] Failed to load glTF: " << e.what() << std::endl;
            return -1;
        }
    }
    
    int loadBrainMesh(const float* vertices, int vertexCount,
                      const unsigned int* faces, int faceCount) {
        // Store mesh data for vertex color updates
        this->vertices.assign(vertices, vertices + vertexCount * 3);
        this->faces.assign(faces, faces + faceCount * 3);
        this->vertexCount = vertexCount;
        
        std::cout << "[BrainViewer] Loaded brain mesh: " << vertexCount << " vertices" << std::endl;
        return 0;
    }
    
    int loadBrainColors(const float* colors, int frameCount, int vertexCount) {
        // Store color time-series data
        // colors layout: [frame][vertex][RGB]
        this->brainColors.assign(colors, colors + frameCount * vertexCount * 3);
        this->frameCount = frameCount;
        this->colorVertexCount = vertexCount;
        
        std::cout << "[BrainViewer] Loaded brain colors: " << frameCount << " frames" << std::endl;
        return 0;
    }
    
    void setTime(double time) {
        currentTime = std::max(0.0, std::min(time, duration));
        updateBrainColor();
    }
    
    void play() { isPlaying = true; }
    void pause() { isPlaying = false; }
    
    int renderFrame() {
        // Handle input
        if (display->shouldClose()) {
            return 0; // Window closed
        }
        
        // Start ImGui frame (placeholder - needs actual implementation)
        // if (imGuiInitialized) {
        //     ImGui_ImplVulkan_NewFrame();
        //     ImGui_ImplGlfw_NewFrame();
        //     ImGui::NewFrame();
        // }
        
        // Draw file selector if visible
        if (showFileSelector) {
            fileSelector.draw();
        }
        
        // Render main content (only when file selector is hidden)
        if (!showFileSelector) {
            // Update time if playing
            if (isPlaying) {
                auto now = std::chrono::steady_clock::now();
                static auto lastTime = now;
                
                std::chrono::duration<double> elapsed = now - lastTime;
                lastTime = now;
                
                currentTime += elapsed.count();
                if (currentTime >= duration) {
                    currentTime = 0; // Loop
                }
                updateBrainColor();
            }
            
            // Render split-screen
            renderSplitScreen();
        }
        
        // Render ImGui (placeholder)
        // if (imGuiInitialized) {
        //     ImGui::Render();
        //     ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), ...);
        // }
        
        return 1; // Continue
    }
    
    double getDuration() const { return duration; }
    double getCurrentTime() const { return currentTime; }
    bool getIsPlaying() const { return isPlaying; }

private:
    void setupImGui() {
        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        
        // Setup style
        ImGui::StyleColorsDark();
        
        // Setup Platform/Renderer bindings
        // Note: These require the GLFW window and Vulkan device from the engine
        // For now, this is a placeholder - actual implementation needs access to engine internals
        // ImGui_ImplGlfw_InitForVulkan(display->window, true);
        // ImGui_ImplVulkan_Init(...);
    }
    
    void onFilePairSelected(const motive::FilePair& pair) {
        std::cout << "[BrainViewer] Selected pair: " << pair.baseName << std::endl;
        
        // Load the selected files
        if (!pair.videoPath.empty()) {
            loadVideo(pair.videoPath.c_str());
        }
        if (!pair.npyPath.empty()) {
            // Load brain data from npy would go here
            std::cout << "[BrainViewer] Brain data: " << pair.npyPath << std::endl;
        }
        
        // Hide file selector and show main view
        showFileSelector = false;
        fileSelector.hide();
    }
    
    void setupCameras() {
        // Left camera for video (orthographic, full-screen quad)
        // Right camera for 3D brain (perspective, orbital)
        
        // For now, use default camera setup
        // The split-screen is handled by viewport scissoring in render
    }
    
    void updateBrainColor() {
        // Update vertex colors based on current time
        if (brainColors.empty() || frameCount == 0) return;
        
        // Calculate which frame to show
        int frame = static_cast<int>((currentTime / duration) * frameCount);
        frame = std::max(0, std::min(frame, frameCount - 1));
        
        // Update model vertex colors
        if (brainModel && colorVertexCount > 0) {
            const float* frameColors = &brainColors[frame * colorVertexCount * 3];
            // This would call into Model to update vertex colors
            // brainModel->updateVertexColors(frameColors, colorVertexCount);
        }
    }
    
    void renderSplitScreen() {
        // Render left half: Video
        // Render right half: Brain
        
        // This is handled by the engine's render loop
        // We configure two viewports:
        // - Left: Video texture on full-screen quad
        // - Right: 3D brain model with vertex colors
        
        engine->renderLoop(); // This runs one frame
    }
    
    std::unique_ptr<Engine> engine;
    Display* display = nullptr;
    Model* brainModel = nullptr;
    
    int width, height;
    std::string videoPath;
    double currentTime;
    double duration;
    bool isPlaying;
    bool showFileSelector;
    
    // File selector UI
    motive::FileSelectorUI fileSelector;
    
    // Brain mesh data
    std::vector<float> vertices;
    std::vector<unsigned int> faces;
    int vertexCount = 0;
    
    // Color animation data
    std::vector<float> brainColors;  // [frame][vertex][RGB]
    int frameCount = 0;
    int colorVertexCount = 0;
};

// C API Implementation

BrainViewerHandle brain_viewer_create(int width, int height) {
    try {
        auto* viewer = new BrainViewer(width, height);
        return viewer;
    } catch (const std::exception& e) {
        std::cerr << "[BrainViewer] Failed to create: " << e.what() << std::endl;
        return nullptr;
    }
}

void brain_viewer_destroy(BrainViewerHandle handle) {
    if (handle) {
        delete static_cast<BrainViewer*>(handle);
    }
}

double brain_viewer_load_video(BrainViewerHandle handle, const char* video_path) {
    if (!handle) return -1.0;
    return static_cast<BrainViewer*>(handle)->loadVideo(video_path);
}

int brain_viewer_load_gltf(BrainViewerHandle handle, const char* gltf_path) {
    if (!handle) return -1;
    return static_cast<BrainViewer*>(handle)->loadGLTF(gltf_path);
}

int brain_viewer_load_brain_mesh(BrainViewerHandle handle,
                                  const float* vertices, int vertex_count,
                                  const unsigned int* faces, int face_count) {
    if (!handle) return -1;
    return static_cast<BrainViewer*>(handle)->loadBrainMesh(vertices, vertex_count, faces, face_count);
}

int brain_viewer_load_brain_colors(BrainViewerHandle handle,
                                    const float* colors,
                                    int frame_count,
                                    int vertex_count) {
    if (!handle) return -1;
    return static_cast<BrainViewer*>(handle)->loadBrainColors(colors, frame_count, vertex_count);
}

void brain_viewer_set_time(BrainViewerHandle handle, double time_seconds) {
    if (handle) {
        static_cast<BrainViewer*>(handle)->setTime(time_seconds);
    }
}

void brain_viewer_play(BrainViewerHandle handle) {
    if (handle) {
        static_cast<BrainViewer*>(handle)->play();
    }
}

void brain_viewer_pause(BrainViewerHandle handle) {
    if (handle) {
        static_cast<BrainViewer*>(handle)->pause();
    }
}

int brain_viewer_render_frame(BrainViewerHandle handle) {
    if (!handle) return 0;
    return static_cast<BrainViewer*>(handle)->renderFrame();
}

double brain_viewer_get_duration(BrainViewerHandle handle) {
    if (!handle) return 0.0;
    return static_cast<BrainViewer*>(handle)->getDuration();
}

double brain_viewer_get_current_time(BrainViewerHandle handle) {
    if (!handle) return 0.0;
    return static_cast<BrainViewer*>(handle)->getCurrentTime();
}

int brain_viewer_is_playing(BrainViewerHandle handle) {
    if (!handle) return 0;
    return static_cast<BrainViewer*>(handle)->getIsPlaying() ? 1 : 0;
}

void brain_viewer_show_file_selector(BrainViewerHandle handle, int show) {
    if (!handle) return;
    BrainViewer* viewer = static_cast<BrainViewer*>(handle);
    if (show) {
        viewer->showFileSelector = true;
        viewer->fileSelector.show();
    } else {
        viewer->showFileSelector = false;
        viewer->fileSelector.hide();
    }
}

int brain_viewer_has_file_selection(BrainViewerHandle handle) {
    if (!handle) return 0;
    return static_cast<BrainViewer*>(handle)->fileSelector.hasSelection() ? 1 : 0;
}

void brain_viewer_get_selected_video_path(BrainViewerHandle handle, char* out_path, int max_len) {
    if (!handle || !out_path || max_len <= 0) return;
    const auto& pair = static_cast<BrainViewer*>(handle)->fileSelector.getSelectedPair();
    strncpy(out_path, pair.videoPath.c_str(), max_len - 1);
    out_path[max_len - 1] = '\0';
}

void brain_viewer_get_selected_npy_path(BrainViewerHandle handle, char* out_path, int max_len) {
    if (!handle || !out_path || max_len <= 0) return;
    const auto& pair = static_cast<BrainViewer*>(handle)->fileSelector.getSelectedPair();
    strncpy(out_path, pair.npyPath.c_str(), max_len - 1);
    out_path[max_len - 1] = '\0';
}
