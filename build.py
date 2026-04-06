#!/usr/bin/env python3
import os
import re
import subprocess
import sys
import shutil
import json
import argparse
from concurrent.futures import ThreadPoolExecutor
import pathlib

def pkg_config_flags(*packages, mode):
    cmd = ["pkg-config", mode, *packages]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        package_list = ", ".join(packages)
        print(f"Missing pkg-config metadata for: {package_list}", file=sys.stderr)
        sys.exit(result.returncode)
    return result.stdout.strip()

def pkg_config_exists(package):
    result = subprocess.run(
        ["pkg-config", "--exists", package],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode == 0

def try_pkg_config_flags(*packages, mode):
    cmd = ["pkg-config", mode, *packages]
    result = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        return None
    return result.stdout.strip()

def try_qt6_fallback_flags():
    include_root_candidates = [
        "/usr/include/x86_64-linux-gnu/qt6",
        "/usr/local/include/qt6",
        "/usr/include/qt6",
    ]
    lib_dir_candidates = [
        "/usr/lib/x86_64-linux-gnu",
        "/usr/local/lib",
    ]

    include_root = next((p for p in include_root_candidates if os.path.isdir(p)), None)
    lib_dir = next((p for p in lib_dir_candidates if os.path.isdir(p)), None)
    if not include_root or not lib_dir:
        return None, None

    required_headers = [
        os.path.join(include_root, "QtWidgets", "QApplication"),
        os.path.join(include_root, "QtGui", "QWindow"),
        os.path.join(include_root, "QtCore", "QObject"),
    ]
    if not all(os.path.exists(p) for p in required_headers):
        return None, None

    required_libs = [
        os.path.join(lib_dir, "libQt6Widgets.so"),
        os.path.join(lib_dir, "libQt6Gui.so"),
        os.path.join(lib_dir, "libQt6Core.so"),
    ]
    if not all(os.path.exists(p) for p in required_libs):
        return None, None

    include_flags = " ".join(
        f"-I{p}"
        for p in [
            include_root,
            os.path.join(include_root, "QtWidgets"),
            os.path.join(include_root, "QtGui"),
            os.path.join(include_root, "QtCore"),
        ]
    )
    link_flags = f"-L{lib_dir} -lQt6Widgets -lQt6Gui -lQt6Core"
    return include_flags, link_flags

# Allow fallbacks when library names differ between static builds and system packages.
LIB_ALIASES = {
    "glfw3": ["glfw"],
}

# Parse command line arguments
parser = argparse.ArgumentParser(description='Build the Motive engine')
parser.add_argument('--rebuild', action='store_true', help='Force rebuild all files, ignoring timestamps')
args = parser.parse_args()
REBUILD = args.rebuild

# Paths
this_dir = os.path.dirname(__file__)
vulkan_sdk_path = os.path.abspath(os.path.join(this_dir, "Vulkan-Headers"))
shader_dir = os.path.abspath(os.path.join(this_dir, "shaders"))
manifest_path = os.path.join(this_dir, ".build_manifest.json")
ffmpeg_install_dir = os.path.abspath(os.path.join(this_dir, "FFmpeg/.build/install"))

# Source and object files
main_sources = ["motive3d.cpp", "motive2d.cpp", "encode.cpp"]
qt_main_sources = ["motive_editor_main.cpp"]
exclude_sources = [
    "brain_viewer_engine.cpp",  # currently does not match the main engine APIs
    "vulkan_video_bridge.cpp",  # missing Vulkan-Video-Samples libraries
]
so_sources = []
for file in os.listdir(this_dir):
    if (
        file.endswith(".cpp")
        and file not in main_sources
        and file not in qt_main_sources
        and file not in exclude_sources
        and not file.startswith("editor_ref_")
    ):
        so_sources.append(file)
        
so_objects = [f"{os.path.splitext(f)[0]}.o" for f in so_sources]
main_objects = [f"{os.path.splitext(f)[0]}.o" for f in main_sources]
qt_main_objects = [f"{os.path.splitext(f)[0]}.o" for f in qt_main_sources]

# Sample Vulkan Video sources (decoder/parsers/utils)
vv_common_sources = [
    str(p) for p in pathlib.Path(this_dir, "common_vv/libs").rglob("*.cpp")
]
vv_decoder_sources = [
    str(p) for p in pathlib.Path(this_dir, "vk_video_decoder/libs").rglob("*.cpp")
]
vv_sources = vv_common_sources + vv_decoder_sources
vv_objects = [os.path.splitext(os.path.relpath(p, this_dir))[0] + ".o" for p in vv_sources]

# Include and library paths
include_paths = [
    os.path.join(vulkan_sdk_path, "include"),
    os.path.abspath(os.path.join(this_dir, "glfw/include")),
    os.path.abspath(os.path.join(this_dir, "tinygltf")),
    os.path.abspath(os.path.join(this_dir, "tinygltf/examples/common/imgui")),
    os.path.abspath(os.path.join(this_dir, "imgui")),
    os.path.abspath(os.path.join(this_dir, "glm")),
    os.path.join(ffmpeg_install_dir, "include"),
    os.path.abspath(os.path.join(this_dir, "freetype/include")),
    os.path.abspath(os.path.join(this_dir, "freetype/build/include")),
    os.path.abspath(os.path.join(this_dir, "freetype/build/include/freetype2")),
    os.path.abspath(os.path.join(this_dir, "common_vv/include")),
    os.path.abspath(os.path.join(this_dir, "common_vv/libs")),
    os.path.abspath(os.path.join(this_dir, "vk_video_decoder/include")),
    os.path.abspath(os.path.join(this_dir, "vk_video_decoder/libs")),
    os.path.abspath(os.path.join(this_dir, "bullet3/src")),
    os.path.abspath(os.path.join(this_dir, "ufbx")),
    os.path.abspath(os.path.join(this_dir, "jolt")),
    # ncnn integration optional; keep out of default include paths.
]
for system_include in ("/usr/include/freetype2", "/usr/local/include/freetype2"):
    if os.path.isdir(system_include):
        include_paths.append(system_include)
ffmpeg_lib_dir = os.path.join(ffmpeg_install_dir, "lib")
lib_paths = [
    os.path.join(vulkan_sdk_path, "lib"),
    os.path.abspath(os.path.join(this_dir, "glfw/build/src")),
    ffmpeg_lib_dir,
    os.path.abspath(os.path.join(this_dir, "freetype/build")),
    # ncnn integration optional; keep out of default library paths.
    os.path.abspath(os.path.join(this_dir, ".")),
    # Bullet Physics library paths
    os.path.abspath(os.path.join(this_dir, "bullet3/build/src/BulletDynamics")),
    os.path.abspath(os.path.join(this_dir, "bullet3/build/src/BulletCollision")),
    os.path.abspath(os.path.join(this_dir, "bullet3/build/src/LinearMath")),
    # ncnn/glslang library paths
    os.path.abspath(os.path.join(this_dir, "ncnn/build/src")),
    os.path.abspath(os.path.join(this_dir, "ncnn/build/glslang/glslang")),
]
core_libraries = [
    "glfw3",
    "tinygltf",
    "vulkan",
    "avformat",
    "avcodec",
    "swscale",
    "avutil",
    "swresample",
    "freetype",
    "m",
    "pthread",
    "dl",
    "gomp",
]



# Optional libraries may not be present on every system; include them only if
# we can actually find an archive or a shared object to link against.
optional_libraries = [
    # Image/codec helpers
    "png",
    "brotlidec",
    "brotlicommon",
    "bz2",
    "lzma",
    # Hardware/video helpers
    "drm",
    "z",
    "OpenCL",
    "X11",
    "Xext",
    "vdpau",
]


def resolve_library(lib_name):
    """
    Return a linker argument for the requested library or None if not found.
    We prefer the standard -l flag when available; otherwise fall back to a
    versioned .so path returned by ldconfig.
    """
    for name in [lib_name] + LIB_ALIASES.get(lib_name, []):
        # First check whether the compiler can already see the library.
        for ext in (".so", ".a"):
            result = subprocess.run(
                ["g++", f"-print-file-name=lib{name}{ext}"],
                capture_output=True,
                text=True,
                check=False,
            )
            path = result.stdout.strip()
            if path and path != f"lib{name}{ext}" and os.path.exists(path):
                return f"-l{name}"

        # Next, look through the configured library paths explicitly.
        for base in lib_paths:
            for ext in (".a", ".so"):
                candidate = os.path.join(base, f"lib{name}{ext}")
                if os.path.exists(candidate):
                    return candidate

        # Finally, consult ldconfig for a versioned shared object.
        if shutil.which("ldconfig"):
            pattern = re.compile(rf"lib{re.escape(name)}[\w\-]*\.so")
            ldconfig_out = subprocess.run(
                ["ldconfig", "-p"], capture_output=True, text=True, check=False
            ).stdout
            for line in ldconfig_out.splitlines():
                if "=>" not in line:
                    continue
                tokens = line.strip().split()
                if not tokens:
                    continue
                soname = tokens[0]
                prefix = f"lib{name}"
                if not soname.startswith(prefix):
                    continue
                suffix = soname[len(prefix) :]
                if suffix and suffix[0] not in ".0123456789":
                    continue
                if not pattern.search(soname):
                    continue
                candidate = line.split("=>")[-1].strip()
                if os.path.exists(candidate):
                    return candidate

    return None


def collect_link_args(required_libs, optional_libs):
    resolved = []
    missing_required = []
    missing_optional = []

    for lib in required_libs:
        arg = resolve_library(lib)
        if arg:
            resolved.append(arg)
        else:
            missing_required.append(lib)

    for lib in optional_libs:
        arg = resolve_library(lib)
        if arg:
            resolved.append(arg)
        else:
            missing_optional.append(lib)

    if missing_required:
        print("Missing required libraries (install dev packages or adjust paths):", ", ".join(missing_required))
        sys.exit(1)

    if missing_optional:
        print("Skipping missing libraries:", ", ".join(missing_optional))

    return resolved


library_link_args = collect_link_args(core_libraries, optional_libraries)

# Add Qt libraries if available
qt_link_flags = try_pkg_config_flags("Qt6Core", "Qt6Gui", "Qt6Widgets", mode="--libs")
if qt_link_flags:
    library_link_args.append(qt_link_flags)

# Always enable Jolt if library is present
if os.path.exists(os.path.abspath(os.path.join(this_dir, "jolt/Build/Linux_Release/libJolt.a"))):
    # Jolt is available - add define for conditional compilation
    # Note: This would need to be passed to the compiler via extra_flags in compile_cpp_to_o
    pass

# Add Bullet libraries directly (they use full paths)
bullet_lib_dir = os.path.abspath(os.path.join(this_dir, "bullet3/build/src"))
bullet_libraries = [
    os.path.join(bullet_lib_dir, "BulletDynamics/libBulletDynamics.a"),
    os.path.join(bullet_lib_dir, "BulletCollision/libBulletCollision.a"),
    os.path.join(bullet_lib_dir, "LinearMath/libLinearMath.a"),
]
for bullet_lib in bullet_libraries:
    if os.path.exists(bullet_lib):
        library_link_args.append(bullet_lib)
    else:
        print(f"Warning: Bullet library not found: {bullet_lib}")

# Add Jolt library if available
jolt_lib = os.path.abspath(os.path.join(this_dir, "jolt/Build/Linux_Release/libJolt.a"))
if os.path.exists(jolt_lib):
    library_link_args.append(jolt_lib)
    print("Jolt Physics library found and linked")
else:
    print("Note: Jolt Physics library not found (optional)")

# Add ncnn/glslang libraries
ncnn_lib_dir = os.path.abspath(os.path.join(this_dir, "ncnn/build"))
ncnn_libraries = [
    os.path.join(ncnn_lib_dir, "glslang/glslang/libglslang.a"),
    os.path.join(ncnn_lib_dir, "glslang/glslang/libglslang-default-resource-limits.a"),
    os.path.join(ncnn_lib_dir, "src/libncnn.a"),
]
for ncnn_lib in ncnn_libraries:
    if os.path.exists(ncnn_lib):
        library_link_args.append(ncnn_lib)
    else:
        print(f"Note: ncnn/glslang library not found: {ncnn_lib}")

# Add ufbx object file if available
ufbx_obj = os.path.abspath(os.path.join(this_dir, "ufbx/ufbx.o"))
if os.path.exists(ufbx_obj):
    library_link_args.append(ufbx_obj)
else:
    # Try to compile ufbx.c
    ufbx_c = os.path.abspath(os.path.join(this_dir, "ufbx/ufbx.c"))
    if os.path.exists(ufbx_c):
        ufbx_compile_cmd = "gcc -c -O2 -fPIC ufbx/ufbx.c -o ufbx/ufbx.o"
        print("Compiling ufbx.c...")
        result = subprocess.run(ufbx_compile_cmd, shell=True, cwd=this_dir)
        if result.returncode == 0 and os.path.exists(ufbx_obj):
            library_link_args.append(ufbx_obj)
qt_packages = ["Qt6Widgets", "Qt6Gui", "Qt6Core"]
qt_include_flags = try_pkg_config_flags(*qt_packages, mode="--cflags")
qt_link_flags = try_pkg_config_flags(*qt_packages, mode="--libs")
if qt_include_flags is None or qt_link_flags is None:
    fallback_include, fallback_link = try_qt6_fallback_flags()
    if fallback_include and fallback_link:
        qt_include_flags = fallback_include
        qt_link_flags = fallback_link
qt_enabled = qt_include_flags is not None and qt_link_flags is not None
if not qt_enabled:
    print("Qt6 pkg-config metadata not found; skipping Qt editor build.")
    qt_include_flags = ""
    qt_link_flags = ""
    qt_main_sources = []
    qt_main_objects = []
    so_sources = [src for src in so_sources if not src.startswith("")]
    so_objects = [f"{os.path.splitext(f)[0]}.o" for f in so_sources]

ffmpeg_packages = ["libavcodec", "libavformat", "libavutil", "libswscale", "libswresample"]
missing_ffmpeg = [pkg for pkg in ffmpeg_packages if not pkg_config_exists(pkg)]
if missing_ffmpeg:
    missing_list = ", ".join(missing_ffmpeg)
    print("Missing FFmpeg development packages (via pkg-config):", missing_list, file=sys.stderr)
    print("Install with: sudo apt-get install -y libavcodec-dev libavformat-dev libavutil-dev libswscale-dev libswresample-dev", file=sys.stderr)
    sys.exit(1)

# Manifest helpers
def load_manifest():
    if os.path.exists(manifest_path):
        try:
            with open(manifest_path, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}
    return {}


def save_manifest(manifest):
    tmp_path = manifest_path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
    os.replace(tmp_path, manifest_path)

# Flags
debug_flags = "-g -O0"
DEBUG_MODE = "NONE"
sanitize_flags = ""
if DEBUG_MODE == "ADDRESS_SANITIZER":
    sanitize_flags = "-fsanitize=address -fno-omit-frame-pointer"

include_flags = " ".join(f"-I{p}" for p in include_paths)
lib_flags = " ".join(f"-L{p}" for p in lib_paths)
lib_links = " ".join(library_link_args)

# Compile shaders
manifest = load_manifest()
manifest.setdefault("shaders", {})
manifest.setdefault("objects", {})
manifest.setdefault("build_py_mtime", 0)

# Recompile shaders only if source is newer than spv
if REBUILD:
    print("Compiling shaders (forced rebuild)...")
else:
    print("Compiling shaders (incremental)...")
shader_exts = {"vert", "frag", "comp"}
for shaderFilename in os.listdir(shader_dir):
    if shaderFilename.split(".")[-1] not in shader_exts:
        continue
    src = os.path.join(shader_dir, shaderFilename)
    dst = os.path.join(shader_dir, f"{shaderFilename}.spv")
    if not REBUILD:
        src_mtime = os.path.getmtime(src)
        dst_mtime = os.path.getmtime(dst) if os.path.exists(dst) else -1
        if src_mtime <= dst_mtime:
            continue
    cmd = f"glslangValidator -V {src} -o {dst}"
    print(f"Running: {cmd}")
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"❌ Failed to compile shader: {shaderFilename}", file=sys.stderr)
        sys.exit(result.returncode)
    manifest["shaders"][shaderFilename] = os.path.getmtime(src)
print("✅ Shader check complete.\n")


def compile_cpp_to_o(src_file):
    obj_file = f"{os.path.splitext(src_file)[0]}.o"
    src_mtime = os.path.getmtime(src_file)
    build_py_mtime = os.path.getmtime(__file__)
    if not REBUILD and os.path.exists(obj_file):
        obj_mtime = os.path.getmtime(obj_file)
        if obj_mtime >= src_mtime and obj_mtime >= build_py_mtime:
            # Skip unchanged
            return False
    
    # Add NCNN_AVAILABLE flag for detection files
    extra_flags = ""
    if "detection" in src_file or "overlay_yolo" in src_file or "motive2d_yolo" in src_file:
        extra_flags = "-DNCNN_AVAILABLE -DNCNN_USE_VULKAN=0"
    if src_file.startswith(""):
        extra_flags = f"{extra_flags} {qt_include_flags}".strip()
    
    cmd = f"g++ -std=c++17 {debug_flags} {sanitize_flags} -fPIC -c {include_flags} {extra_flags} {src_file} -o {obj_file}"
    print(f"Compiling {src_file}...")
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"❌ Failed to compile: {src_file}", file=sys.stderr)
        sys.exit(result.returncode)
    manifest["objects"][obj_file] = src_mtime
    return True


all_sources = so_sources + main_sources + qt_main_sources + vv_sources
with ThreadPoolExecutor() as executor:
    changed_list = list(executor.map(compile_cpp_to_o, all_sources))

# Link static library only when needed
need_link_so = not os.path.exists("libengine.a")
if not need_link_so and not REBUILD:
    so_mtime = os.path.getmtime("libengine.a")
    for obj in so_objects:
        if os.path.exists(obj) and os.path.getmtime(obj) > so_mtime:
            need_link_so = True
            break

if REBUILD or need_link_so or any(changed_list):
    so_link_cmd = f"ar rcs libengine.a {' '.join(so_objects)}"
    print(f"\nLinking static library:\n{so_link_cmd}")
    so_link_result = subprocess.run(so_link_cmd, shell=True)
    if so_link_result.returncode != 0:
        print("❌ Failed to link static library.", file=sys.stderr)
        sys.exit(so_link_result.returncode)
else:
    print("\nStatic library up to date.")

# Link main executables when needed
for src, obj in zip(main_sources, main_objects):
    binary_name = os.path.splitext(src)[0]
    need_link = not os.path.exists(binary_name)
    if not need_link and not REBUILD:
        bin_mtime = os.path.getmtime(binary_name)
        deps = [obj, "libengine.a"]
        for dep in deps:
            if os.path.exists(dep) and os.path.getmtime(dep) > bin_mtime:
                need_link = True
                break
    if REBUILD or need_link:
        main_link_cmd = f"g++ {debug_flags} {sanitize_flags} {lib_flags} {obj} -L. -lengine {lib_links} -o {binary_name}"
        print(f"\nLinking executable {binary_name}:\n{main_link_cmd}")
        main_link_result = subprocess.run(main_link_cmd, shell=True)
        if main_link_result.returncode != 0:
            print(f"❌ Failed to link executable {binary_name}.", file=sys.stderr)
            sys.exit(main_link_result.returncode)
    else:
        print(f"\nExecutable {binary_name} up to date.")

# Link Qt editor executable when needed
for src, obj in zip(qt_main_sources, qt_main_objects):
    binary_name = "motive_editor"
    need_link = not os.path.exists(binary_name)
    if not need_link and not REBUILD:
        bin_mtime = os.path.getmtime(binary_name)
        deps = [obj, "libengine.a"]
        for dep in deps:
            if os.path.exists(dep) and os.path.getmtime(dep) > bin_mtime:
                need_link = True
                break
    if REBUILD or need_link:
        main_link_cmd = (
            f"g++ {debug_flags} {sanitize_flags} {lib_flags} {obj} -L. -lengine "
            f"{lib_links} {qt_link_flags} -o {binary_name}"
        )
        print(f"\nLinking executable {binary_name}:\n{main_link_cmd}")
        main_link_result = subprocess.run(main_link_cmd, shell=True)
        if main_link_result.returncode != 0:
            print(f"❌ Failed to link executable {binary_name}.", file=sys.stderr)
            sys.exit(main_link_result.returncode)
    else:
        print(f"\nExecutable {binary_name} up to date.")

manifest["build_py_mtime"] = os.path.getmtime(__file__)
save_manifest(manifest)

print("\n✅ Build successful!")
