#!/usr/bin/env python3
"""
file_selector.py - File browser for TribeV2 Brain Viewer

Shows directory tree on the left, automatically pairs video files with 
TribeV2 .npy brain prediction files based on naming conventions.

Naming convention:
    video.mp4 -> video_tribe.npy
    subject01_session1.mp4 -> subject01_session1_tribe.npy
"""

import os
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path
from typing import Callable, Dict, List, Optional, Tuple
import threading


class FilePair:
    """Represents a matched pair of video and brain data files."""
    
    def __init__(self, video_path: Optional[str] = None, npy_path: Optional[str] = None):
        self.video_path = video_path
        self.npy_path = npy_path
        self.base_name = ""
        
        if video_path:
            self.base_name = Path(video_path).stem
        elif npy_path:
            # Remove _tribe suffix if present
            base = Path(npy_path).stem
            if base.endswith("_tribe"):
                base = base[:-6]
            self.base_name = base
    
    @property
    def is_complete(self) -> bool:
        """Returns True if both video and npy are present."""
        return self.video_path is not None and self.npy_path is not None
    
    @property
    def status(self) -> str:
        if self.is_complete:
            return "✓ Complete"
        elif self.video_path:
            return "⚠ Missing brain data"
        elif self.npy_path:
            return "⚠ Missing video"
        return "✗ Empty"
    
    def __repr__(self):
        return f"FilePair({self.base_name}: video={self.video_path is not None}, npy={self.npy_path is not None})"


class FileSelector:
    """
    File browser UI for selecting video/brain data pairs.
    
    Layout:
        +------------------+------------------+
        |  Directory Tree  |  File Pairs List |
        |  (Left Sidebar)  |  (Main Content)  |
        |                  |                  |
        |  [Folders]       |  [Video+NPY]     |
        |   - Project1     |   Pair 1         |
        |   - Project2     |   Pair 2         |
        |                  |                  |
        +------------------+------------------+
        |  Status: X pairs found              |
        +-------------------------------------+
    """
    
    VIDEO_EXTENSIONS = {'.mp4', '.avi', '.mkv', '.mov', '.webm', '.m4v'}
    BRAIN_EXTENSIONS = {'.npy'}
    
    def __init__(self, on_select_callback: Optional[Callable[[FilePair], None]] = None):
        self.root = tk.Tk()
        self.root.title("TribeV2 Brain Viewer - File Selector")
        self.root.geometry("900x600")
        self.root.minsize(600, 400)
        
        self.current_dir: Path = Path.home()
        self.file_pairs: Dict[str, FilePair] = {}
        self.on_select_callback = on_select_callback
        self.selected_pair: Optional[FilePair] = None
        
        self._setup_ui()
        self._populate_directory_tree()
        self.refresh_file_list()
    
    def _setup_ui(self):
        """Initialize the UI components."""
        # Main container with padding
        main_frame = ttk.Frame(self.root, padding="10")
        main_frame.grid(row=0, column=0, sticky="nsew")
        
        # Configure grid weights
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)
        main_frame.columnconfigure(1, weight=1)
        main_frame.rowconfigure(0, weight=1)
        
        # === LEFT SIDEBAR: Directory Tree ===
        left_frame = ttk.LabelFrame(main_frame, text="Directories", padding="5")
        left_frame.grid(row=0, column=0, sticky="nsew", padx=(0, 10))
        left_frame.columnconfigure(0, weight=1)
        left_frame.rowconfigure(1, weight=1)
        
        # Directory controls
        dir_btn_frame = ttk.Frame(left_frame)
        dir_btn_frame.grid(row=0, column=0, sticky="ew", pady=(0, 5))
        
        ttk.Button(dir_btn_frame, text="📁 Open...", 
                   command=self._browse_directory).pack(side=tk.LEFT, padx=2)
        ttk.Button(dir_btn_frame, text="🔄", 
                   command=self.refresh_file_list, width=3).pack(side=tk.LEFT, padx=2)
        
        # Directory treeview
        tree_frame = ttk.Frame(left_frame)
        tree_frame.grid(row=1, column=0, sticky="nsew")
        tree_frame.columnconfigure(0, weight=1)
        tree_frame.rowconfigure(0, weight=1)
        
        self.dir_tree = ttk.Treeview(tree_frame, selectmode="browse", show="tree")
        self.dir_tree.grid(row=0, column=0, sticky="nsew")
        
        dir_scroll = ttk.Scrollbar(tree_frame, orient="vertical", command=self.dir_tree.yview)
        dir_scroll.grid(row=0, column=1, sticky="ns")
        self.dir_tree.configure(yscrollcommand=dir_scroll.set)
        
        self.dir_tree.bind("<<TreeviewSelect>>", self._on_dir_select)
        
        # === RIGHT PANEL: File Pairs ===
        right_frame = ttk.LabelFrame(main_frame, text="Video + Brain Data Pairs", padding="5")
        right_frame.grid(row=0, column=1, sticky="nsew")
        right_frame.columnconfigure(0, weight=1)
        right_frame.rowconfigure(1, weight=1)
        
        # Filter/search box
        filter_frame = ttk.Frame(right_frame)
        filter_frame.grid(row=0, column=0, sticky="ew", pady=(0, 5))
        
        ttk.Label(filter_frame, text="Filter:").pack(side=tk.LEFT, padx=(0, 5))
        self.filter_var = tk.StringVar()
        self.filter_var.trace("w", lambda *args: self._apply_filter())
        ttk.Entry(filter_frame, textvariable=self.filter_var).pack(side=tk.LEFT, fill=tk.X, expand=True)
        
        # Pairs list
        list_frame = ttk.Frame(right_frame)
        list_frame.grid(row=1, column=0, sticky="nsew")
        list_frame.columnconfigure(0, weight=1)
        list_frame.rowconfigure(0, weight=1)
        
        # Treeview for file pairs
        columns = ("status", "video", "brain", "duration")
        self.pairs_tree = ttk.Treeview(list_frame, columns=columns, show="headings", selectmode="browse")
        self.pairs_tree.grid(row=0, column=0, sticky="nsew")
        
        # Column headings
        self.pairs_tree.heading("status", text="Status")
        self.pairs_tree.heading("video", text="Video File")
        self.pairs_tree.heading("brain", text="Brain Data")
        self.pairs_tree.heading("duration", text="Duration")
        
        self.pairs_tree.column("status", width=120, anchor="center")
        self.pairs_tree.column("video", width=250)
        self.pairs_tree.column("brain", width=250)
        self.pairs_tree.column("duration", width=80, anchor="center")
        
        pairs_scroll = ttk.Scrollbar(list_frame, orient="vertical", command=self.pairs_tree.yview)
        pairs_scroll.grid(row=0, column=1, sticky="ns")
        self.pairs_tree.configure(yscrollcommand=pairs_scroll.set)
        
        self.pairs_tree.bind("<<TreeviewSelect>>", self._on_pair_select)
        self.pairs_tree.bind("<Double-1>", self._on_pair_double_click)
        
        # === BOTTOM: Status and Actions ===
        bottom_frame = ttk.Frame(main_frame)
        bottom_frame.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(10, 0))
        
        self.status_label = ttk.Label(bottom_frame, text="No directory selected")
        self.status_label.pack(side=tk.LEFT)
        
        btn_frame = ttk.Frame(bottom_frame)
        btn_frame.pack(side=tk.RIGHT)
        
        ttk.Button(btn_frame, text="Open Selected", 
                   command=self._open_selected, state="disabled").pack(side=tk.LEFT, padx=2)
        self.open_btn = btn_frame.winfo_children()[-1]
        
        ttk.Button(btn_frame, text="Exit", 
                   command=self.root.quit).pack(side=tk.LEFT, padx=2)
    
    def _populate_directory_tree(self):
        """Populate the directory tree with common locations."""
        self.dir_tree.delete(*self.dir_tree.get_children())
        
        # Add root node
        root_node = self.dir_tree.insert("", "end", text="Computer", open=True)
        
        # Add common locations
        common_dirs = [
            ("Home", Path.home()),
            ("Desktop", Path.home() / "Desktop"),
            ("Documents", Path.home() / "Documents"),
            ("Videos", Path.home() / "Videos"),
        ]
        
        for name, path in common_dirs:
            if path.exists():
                node = self.dir_tree.insert(root_node, "end", text=name, values=(str(path),))
                # Add placeholder for lazy loading
                self.dir_tree.insert(node, "end", text="Loading...")
        
        # Add current/working directory
        cwd = Path.cwd()
        if cwd != Path.home():
            node = self.dir_tree.insert(root_node, "end", text="Current", values=(str(cwd),))
            self.dir_tree.insert(node, "end", text="Loading...")
    
    def _browse_directory(self):
        """Open directory browser dialog."""
        directory = filedialog.askdirectory(initialdir=str(self.current_dir))
        if directory:
            self.current_dir = Path(directory)
            self.refresh_file_list()
            
            # Try to select this directory in the tree
            self._select_directory_in_tree(self.current_dir)
    
    def _select_directory_in_tree(self, path: Path):
        """Find and select a directory in the tree."""
        for item in self.dir_tree.get_children():
            for child in self.dir_tree.get_children(item):
                values = self.dir_tree.item(child, "values")
                if values and Path(values[0]) == path:
                    self.dir_tree.selection_set(child)
                    self.dir_tree.see(child)
                    return
    
    def _on_dir_select(self, event):
        """Handle directory selection."""
        selection = self.dir_tree.selection()
        if not selection:
            return
        
        item = selection[0]
        values = self.dir_tree.item(item, "values")
        
        if not values:
            # Lazy load children
            text = self.dir_tree.item(item, "text")
            parent_values = self.dir_tree.item(self.dir_tree.parent(item), "values")
            if parent_values:
                base_path = Path(parent_values[0])
                path = base_path / text
            else:
                return
        else:
            path = Path(values[0])
        
        if path.is_dir():
            self.current_dir = path
            self.refresh_file_list()
            
            # Lazy load subdirectories
            if not values or self.dir_tree.get_children(item):
                # Check if we need to load children
                children = self.dir_tree.get_children(item)
                if children and self.dir_tree.item(children[0], "text") == "Loading...":
                    self.dir_tree.delete(*children)
                    try:
                        for subdir in sorted(path.iterdir()):
                            if subdir.is_dir() and not subdir.name.startswith("."):
                                node = self.dir_tree.insert(item, "end", text=subdir.name, 
                                                            values=(str(subdir),))
                                # Add placeholder for deeper nesting
                                try:
                                    if any(d.is_dir() for d in subdir.iterdir()):
                                        self.dir_tree.insert(node, "end", text="Loading...")
                                except PermissionError:
                                    pass
                    except PermissionError:
                        pass
    
    def refresh_file_list(self):
        """Scan directory and populate file pairs."""
        self.file_pairs.clear()
        
        if not self.current_dir.exists():
            self.status_label.config(text=f"Directory not found: {self.current_dir}")
            return
        
        # Scan for files in a separate thread to avoid blocking UI
        threading.Thread(target=self._scan_directory, daemon=True).start()
    
    def _scan_directory(self):
        """Scan directory for video/brain file pairs."""
        videos: Dict[str, str] = {}
        brains: Dict[str, str] = {}
        
        try:
            for item in self.current_dir.iterdir():
                if item.is_file():
                    suffix = item.suffix.lower()
                    stem = item.stem
                    
                    if suffix in self.VIDEO_EXTENSIONS:
                        videos[stem] = str(item)
                    elif suffix in self.BRAIN_EXTENSIONS:
                        # Remove _tribe suffix for matching
                        if stem.endswith("_tribe"):
                            base = stem[:-6]
                        else:
                            base = stem
                        brains[base] = str(item)
        except PermissionError as e:
            self.root.after(0, lambda: self.status_label.config(text=f"Permission denied: {e}"))
            return
        
        # Match pairs
        all_bases = set(videos.keys()) | set(brains.keys())
        
        for base in all_bases:
            pair = FilePair(
                video_path=videos.get(base),
                npy_path=brains.get(base)
            )
            self.file_pairs[base] = pair
        
        # Update UI in main thread
        self.root.after(0, self._update_pairs_list)
    
    def _update_pairs_list(self):
        """Update the pairs treeview with scanned data."""
        # Clear existing items
        for item in self.pairs_tree.get_children():
            self.pairs_tree.delete(item)
        
        # Add pairs
        for base_name in sorted(self.file_pairs.keys()):
            pair = self.file_pairs[base_name]
            
            # Determine display values
            if pair.is_complete:
                status = "✓"
                tag = "complete"
            elif pair.video_path:
                status = "⚠ Missing NPY"
                tag = "partial"
            else:
                status = "⚠ Missing video"
                tag = "partial"
            
            video_name = Path(pair.video_path).name if pair.video_path else "-"
            brain_name = Path(pair.npy_path).name if pair.npy_path else "-"
            
            # Get video duration if available
            duration = "-"
            if pair.video_path:
                duration = self._get_video_duration(pair.video_path)
            
            self.pairs_tree.insert("", "end", values=(status, video_name, brain_name, duration),
                                   tags=(tag,), iid=base_name)
        
        # Configure tags
        self.pairs_tree.tag_configure("complete", foreground="green")
        self.pairs_tree.tag_configure("partial", foreground="orange")
        
        # Update status
        complete_count = sum(1 for p in self.file_pairs.values() if p.is_complete)
        total_count = len(self.file_pairs)
        self.status_label.config(
            text=f"{self.current_dir} | {complete_count} complete / {total_count} total pairs"
        )
    
    def _get_video_duration(self, video_path: str) -> str:
        """Get video duration in seconds (simplified, no ffprobe dependency)."""
        # Return file size as proxy for now
        try:
            size_mb = Path(video_path).stat().st_size / (1024 * 1024)
            return f"{size_mb:.0f} MB"
        except:
            return "-"
    
    def _apply_filter(self):
        """Filter the pairs list based on search text."""
        filter_text = self.filter_var.get().lower()
        
        for item in self.pairs_tree.get_children():
            values = self.pairs_tree.item(item, "values")
            visible = any(filter_text in str(v).lower() for v in values)
            
            if visible:
                self.pairs_tree.reattach(item, "", "end")
            else:
                self.pairs_tree.detach(item)
    
    def _on_pair_select(self, event):
        """Handle pair selection."""
        selection = self.pairs_tree.selection()
        if selection:
            base_name = selection[0]
            self.selected_pair = self.file_pairs.get(base_name)
            self.open_btn.config(state="normal" if self.selected_pair and self.selected_pair.is_complete else "disabled")
        else:
            self.selected_pair = None
            self.open_btn.config(state="disabled")
    
    def _on_pair_double_click(self, event):
        """Handle double-click on a pair."""
        self._open_selected()
    
    def _open_selected(self):
        """Open the selected file pair."""
        if self.selected_pair and self.selected_pair.is_complete:
            if self.on_select_callback:
                self.on_select_callback(self.selected_pair)
            else:
                # Default: launch brain viewer
                self._launch_brain_viewer(self.selected_pair)
    
    def _launch_brain_viewer(self, pair: FilePair):
        """Launch the brain viewer with the selected pair."""
        import subprocess
        import sys
        
        script_dir = Path(__file__).parent
        viewer_script = script_dir / "brain_viewer.py"
        
        if not viewer_script.exists():
            messagebox.showerror("Error", f"Brain viewer not found: {viewer_script}")
            return
        
        cmd = [
            sys.executable,
            str(viewer_script),
            "--video", pair.video_path,
            "--npy", pair.npy_path
        ]
        
        try:
            subprocess.Popen(cmd)
            self.root.iconify()  # Minimize file selector
        except Exception as e:
            messagebox.showerror("Error", f"Failed to launch viewer: {e}")
    
    def run(self):
        """Start the file selector main loop."""
        self.root.mainloop()
    
    def get_selected(self) -> Optional[FilePair]:
        """Get the currently selected file pair."""
        return self.selected_pair


def select_files(initial_dir: Optional[str] = None) -> Optional[FilePair]:
    """
    Convenience function to open file selector and return selected pair.
    
    Usage:
        pair = select_files("/path/to/data")
        if pair:
            print(f"Selected: {pair.video_path} + {pair.npy_path}")
    """
    result = None
    
    def callback(pair: FilePair):
        nonlocal result
        result = pair
        selector.root.quit()
    
    selector = FileSelector(on_select_callback=callback)
    
    if initial_dir:
        selector.current_dir = Path(initial_dir)
        selector.refresh_file_list()
    
    selector.run()
    return result


if __name__ == "__main__":
    import sys
    
    # Support command-line usage
    initial_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    
    pair = select_files(initial_dir)
    
    if pair:
        print(f"\nSelected pair:")
        print(f"  Video: {pair.video_path}")
        print(f"  Brain: {pair.npy_path}")
    else:
        print("No pair selected")
