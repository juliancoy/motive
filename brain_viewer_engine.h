#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Opaque handle to brain viewer instance
typedef void* BrainViewerHandle;

// Create/destroy brain viewer
BrainViewerHandle brain_viewer_create(int width, int height);
void brain_viewer_destroy(BrainViewerHandle handle);

// Load content
double brain_viewer_load_video(BrainViewerHandle handle, const char* video_path);
int brain_viewer_load_gltf(BrainViewerHandle handle, const char* gltf_path);
int brain_viewer_load_brain_mesh(BrainViewerHandle handle, 
                                  const float* vertices, int vertex_count,
                                  const unsigned int* faces, int face_count);
int brain_viewer_load_brain_colors(BrainViewerHandle handle,
                                    const float* colors,  // RGB per vertex per frame
                                    int frame_count,
                                    int vertex_count);

// Playback control
void brain_viewer_set_time(BrainViewerHandle handle, double time_seconds);
void brain_viewer_play(BrainViewerHandle handle);
void brain_viewer_pause(BrainViewerHandle handle);

// Render loop - returns 0 when window should close
int brain_viewer_render_frame(BrainViewerHandle handle);

// Get state
double brain_viewer_get_duration(BrainViewerHandle handle);
double brain_viewer_get_current_time(BrainViewerHandle handle);
int brain_viewer_is_playing(BrainViewerHandle handle);

// File selector functions
void brain_viewer_show_file_selector(BrainViewerHandle handle, int show);
int brain_viewer_has_file_selection(BrainViewerHandle handle);
void brain_viewer_get_selected_video_path(BrainViewerHandle handle, char* out_path, int max_len);
void brain_viewer_get_selected_npy_path(BrainViewerHandle handle, char* out_path, int max_len);

#ifdef __cplusplus
}
#endif
