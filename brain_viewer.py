#!/usr/bin/env python3
"""
brain_viewer.py - Python wrapper for TribeV2 Brain Viewer C++ Engine

Usage:
    python brain_viewer.py --video video.mp4 --npy video_tribe.npy
"""

import ctypes
import os
import sys
import time
import argparse
import numpy as np
from pathlib import Path

# Load the engine library
_lib = None
_lib_paths = [
    os.path.join(os.path.dirname(__file__), 'libengine.so'),
    os.path.join(os.path.dirname(__file__), 'build/libengine.so'),
]

for path in _lib_paths:
    if os.path.exists(path):
        try:
            _lib = ctypes.CDLL(path)
            print(f"[BrainViewer] Loaded engine from {path}")
            break
        except Exception as e:
            print(f"[BrainViewer] Failed to load {path}: {e}")

if _lib is None:
    raise RuntimeError("Could not load libengine.so. Build the engine first with: python build.py")


# Define C function signatures
_lib.brain_viewer_create.argtypes = [ctypes.c_int, ctypes.c_int]
_lib.brain_viewer_create.restype = ctypes.c_void_p

_lib.brain_viewer_destroy.argtypes = [ctypes.c_void_p]

_lib.brain_viewer_load_video.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_lib.brain_viewer_load_video.restype = ctypes.c_double

_lib.brain_viewer_load_gltf.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
_lib.brain_viewer_load_gltf.restype = ctypes.c_int

_lib.brain_viewer_load_brain_mesh.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_float), ctypes.c_int,
    ctypes.POINTER(ctypes.c_uint32), ctypes.c_int
]
_lib.brain_viewer_load_brain_mesh.restype = ctypes.c_int

_lib.brain_viewer_load_brain_colors.argtypes = [
    ctypes.c_void_p,
    ctypes.POINTER(ctypes.c_float), ctypes.c_int, ctypes.c_int
]
_lib.brain_viewer_load_brain_colors.restype = ctypes.c_int

_lib.brain_viewer_set_time.argtypes = [ctypes.c_void_p, ctypes.c_double]

_lib.brain_viewer_play.argtypes = [ctypes.c_void_p]
_lib.brain_viewer_pause.argtypes = [ctypes.c_void_p]

_lib.brain_viewer_render_frame.argtypes = [ctypes.c_void_p]
_lib.brain_viewer_render_frame.restype = ctypes.c_int

_lib.brain_viewer_get_duration.argtypes = [ctypes.c_void_p]
_lib.brain_viewer_get_duration.restype = ctypes.c_double

_lib.brain_viewer_get_current_time.argtypes = [ctypes.c_void_p]
_lib.brain_viewer_get_current_time.restype = ctypes.c_double

_lib.brain_viewer_is_playing.argtypes = [ctypes.c_void_p]
_lib.brain_viewer_is_playing.restype = ctypes.c_int

# File selector functions
_lib.brain_viewer_show_file_selector.argtypes = [ctypes.c_void_p, ctypes.c_int]
_lib.brain_viewer_has_file_selection.argtypes = [ctypes.c_void_p]
_lib.brain_viewer_has_file_selection.restype = ctypes.c_int
_lib.brain_viewer_get_selected_video_path.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
_lib.brain_viewer_get_selected_npy_path.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]


class BrainViewer:
    """
    Split-screen viewer showing video alongside animated brain model.
    Implemented in C++ with Vulkan, controlled via Python.
    """
    
    def __init__(self, width: int = 1920, height: int = 1080):
        self._handle = _lib.brain_viewer_create(width, height)
        if not self._handle:
            raise RuntimeError("Failed to create BrainViewer")
        
        self._width = width
        self._height = height
        self._brain_data = None
        
        print(f"[BrainViewer] Created {width}x{height}")
    
    def __del__(self):
        if self._handle:
            _lib.brain_viewer_destroy(self._handle)
            self._handle = None
    
    def load_video(self, video_path: str) -> float:
        """Load video and return duration in seconds."""
        duration = _lib.brain_viewer_load_video(self._handle, video_path.encode('utf-8'))
        print(f"[BrainViewer] Loaded video: {video_path} ({duration:.2f}s)")
        return duration
    
    def load_brain_gltf(self, gltf_path: str):
        """Load pre-exported brain glTF."""
        result = _lib.brain_viewer_load_gltf(self._handle, gltf_path.encode('utf-8'))
        if result != 0:
            raise RuntimeError(f"Failed to load glTF: {gltf_path}")
        print(f"[BrainViewer] Loaded glTF: {gltf_path}")
    
    def load_brain_npy(self, npy_path: str, mesh: str = 'fsaverage5'):
        """
        Load TribeV2 predictions from .npy and upload to GPU.
        
        Args:
            npy_path: Path to _tribe.npy file
            mesh: Brain mesh type ('fsaverage5', etc.)
        """
        data = np.load(npy_path)
        n_timesteps, n_vertices = data.shape
        
        print(f"[BrainViewer] Loading brain data: {n_timesteps} frames, {n_vertices} vertices")
        
        # Generate fsaverage5 mesh
        vertices, faces = self._generate_fsaverage5_mesh()
        
        # Upload mesh
        vertex_ptr = vertices.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        face_ptr = faces.ctypes.data_as(ctypes.POINTER(ctypes.c_uint32))
        
        result = _lib.brain_viewer_load_brain_mesh(
            self._handle, vertex_ptr, len(vertices), face_ptr, len(faces)
        )
        if result != 0:
            raise RuntimeError("Failed to load brain mesh")
        
        # Generate and upload colors
        colors = self._brain_data_to_colors(data)
        color_ptr = colors.ctypes.data_as(ctypes.POINTER(ctypes.c_float))
        
        result = _lib.brain_viewer_load_brain_colors(
            self._handle, color_ptr, n_timesteps, n_vertices
        )
        if result != 0:
            raise RuntimeError("Failed to load brain colors")
        
        self._brain_data = data
        print("[BrainViewer] Brain data uploaded to GPU")
        return n_timesteps
    
    def _generate_fsaverage5_mesh(self):
        """Generate fsaverage5 cortical surface mesh."""
        try:
            from nibabel import freesurfer
            from nilearn import datasets
            
            fs = datasets.fetch_surf_fsaverage('fsaverage5')
            
            # Load both hemispheres
            coords_l, faces_l = freesurfer.read_geometry(fs['pial_left'])
            coords_r, faces_r = freesurfer.read_geometry(fs['pial_right'])
            
            # Offset right hemisphere
            coords_r[:, 0] += coords_l[:, 0].max() + 20
            
            # Combine
            vertices = np.vstack([coords_l, coords_r]).astype(np.float32)
            faces = np.vstack([faces_l, faces_r + len(coords_l)]).astype(np.uint32)
            
            return vertices, faces
            
        except Exception as e:
            print(f"[BrainViewer] Could not load fsaverage: {e}")
            print("[BrainViewer] Using placeholder sphere mesh")
            return self._generate_placeholder_mesh()
    
    def _generate_placeholder_mesh(self):
        """Generate simple sphere as placeholder."""
        n = 1000
        phi = np.linspace(0, np.pi, n)
        theta = np.linspace(0, 2*np.pi, n)
        phi, theta = np.meshgrid(phi, theta)
        
        x = np.sin(phi) * np.cos(theta)
        y = np.sin(phi) * np.sin(theta)
        z = np.cos(phi)
        
        left = np.column_stack([x.flatten(), y.flatten(), z.flatten()])
        left[:, 0] -= 1.5
        
        right = left.copy()
        right[:, 0] += 3.0
        
        vertices = np.vstack([left, right]).astype(np.float32) * 50
        faces = np.array([[i, i+1, i+2] for i in range(0, len(vertices)-3, 3)], dtype=np.uint32)
        
        return vertices, faces
    
    def _brain_data_to_colors(self, data: np.ndarray) -> np.ndarray:
        """Convert BOLD values to RGB using fire colormap."""
        vmin, vmax = np.percentile(data, [1, 99])
        normalized = np.clip((data - vmin) / (vmax - vmin), 0, 1)
        
        colors = np.zeros((*data.shape, 3), dtype=np.float32)
        colors[:, :, 0] = np.clip(normalized * 2, 0, 1)  # Red
        colors[:, :, 1] = np.clip((normalized - 0.5) * 2, 0, 1)  # Green
        colors[:, :, 2] = np.clip((normalized - 0.75) * 4, 0, 1)  # Blue
        
        return colors
    
    def set_time(self, time_seconds: float):
        """Set current playback time."""
        _lib.brain_viewer_set_time(self._handle, time_seconds)
    
    def play(self):
        """Start playback."""
        _lib.brain_viewer_play(self._handle)
        print("[BrainViewer] Playback started")
    
    def pause(self):
        """Pause playback."""
        _lib.brain_viewer_pause(self._handle)
        print("[BrainViewer] Playback paused")
    
    @property
    def duration(self) -> float:
        """Get video duration."""
        return _lib.brain_viewer_get_duration(self._handle)
    
    @property
    def current_time(self) -> float:
        """Get current playback time."""
        return _lib.brain_viewer_get_current_time(self._handle)
    
    @property
    def is_playing(self) -> bool:
        """Check if currently playing."""
        return _lib.brain_viewer_is_playing(self._handle) != 0
    
    def show_file_selector(self):
        """Show the built-in file selector UI."""
        _lib.brain_viewer_show_file_selector(self._handle, 1)
    
    def hide_file_selector(self):
        """Hide the file selector UI."""
        _lib.brain_viewer_show_file_selector(self._handle, 0)
    
    def has_file_selection(self) -> bool:
        """Check if user has selected a file pair in the UI."""
        return _lib.brain_viewer_has_file_selection(self._handle) != 0
    
    def get_selected_files(self) -> Tuple[Optional[str], Optional[str]]:
        """Get the selected video and NPY paths from the file selector."""
        video_buf = ctypes.create_string_buffer(1024)
        npy_buf = ctypes.create_string_buffer(1024)
        
        _lib.brain_viewer_get_selected_video_path(self._handle, video_buf, 1024)
        _lib.brain_viewer_get_selected_npy_path(self._handle, npy_buf, 1024)
        
        video_path = video_buf.value.decode('utf-8') if video_buf.value else None
        npy_path = npy_buf.value.decode('utf-8') if npy_buf.value else None
        
        return video_path, npy_path
    
    def _infer_npy_from_video(self, video_path: str) -> Optional[str]:
        """
        Infer the NPY file path from the video path.
        Naming convention: video.mp4 -> video_tribe.npy
        """
        if not video_path:
            return None
        
        from pathlib import Path
        video = Path(video_path)
        
        # Try video_stem_tribe.npy
        inferred_npy = video.parent / f"{video.stem}_tribe.npy"
        
        if inferred_npy.exists():
            print(f"[BrainViewer] Inferred NPY: {inferred_npy}")
            return str(inferred_npy)
        
        # Also try with _tribe.npy suffix if video has _tribe in name
        if video.stem.endswith('_tribe'):
            # Already a tribe file, look for base name
            base_name = video.stem[:-6]  # Remove '_tribe'
            inferred_npy = video.parent / f"{base_name}_tribe.npy"
            if inferred_npy.exists():
                print(f"[BrainViewer] Inferred NPY: {inferred_npy}")
                return str(inferred_npy)
        
        print(f"[BrainViewer] Warning: Could not find NPY for {video_path}")
        print(f"[BrainViewer] Looked for: {inferred_npy}")
        return None
    
    def run(self):
        """Main render loop. Blocks until window closes."""
        print("[BrainViewer] Starting render loop...")
        print("[BrainViewer] Controls: SPACE=play/pause, LEFT/RIGHT=scrub, Q=quit")
        
        try:
            while _lib.brain_viewer_render_frame(self._handle):
                # Check if file selection was made
                if self.has_file_selection():
                    video, npy = self.get_selected_files()
                    
                    # Infer NPY from video if not provided
                    if video and not npy:
                        npy = self._infer_npy_from_video(video)
                    
                    if video and npy:
                        print(f"[BrainViewer] Loading: {video}")
                        print(f"[BrainViewer] Brain data: {npy}")
                        self.load_video(video)
                        self.load_brain_npy(npy)
                        self.hide_file_selector()
                        self.play()
                    elif video:
                        print(f"[BrainViewer] Error: No NPY file found for {video}")
                        self.hide_file_selector()
        except KeyboardInterrupt:
            pass
        
        print("[BrainViewer] Render loop ended")


def main():
    parser = argparse.ArgumentParser(description='TribeV2 Brain Viewer')
    parser.add_argument('--video', '-v', help='Video file path')
    parser.add_argument('--npy', '-n', help='TribeV2 .npy file')
    parser.add_argument('--no-selector', action='store_true', help='Skip file selector, use --video and --npy')
    parser.add_argument('--width', type=int, default=1920, help='Window width')
    parser.add_argument('--height', type=int, default=1080, help='Window height')
    
    args = parser.parse_args()
    
    # Create viewer
    viewer = BrainViewer(width=args.width, height=args.height)
    
    # If video specified, infer NPY if not provided
    if args.video:
        if not args.npy:
            args.npy = viewer._infer_npy_from_video(args.video)
        
        if args.npy:
            viewer.load_video(args.video)
            viewer.load_brain_npy(args.npy)
            viewer.play()
        else:
            print(f"[BrainViewer] Error: Could not find NPY for {args.video}")
            print("[BrainViewer] Use --npy to specify the NPY file explicitly")
            return
    else:
        # Show integrated file selector (C++ ImGui)
        print("[BrainViewer] Showing file selector...")
        viewer.show_file_selector()
    
    # Run main loop
    viewer.run()


if __name__ == "__main__":
    main()
