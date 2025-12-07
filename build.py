#!/usr/bin/env python3
import os
import re
import subprocess
import sys
import shutil
from concurrent.futures import ThreadPoolExecutor

# Paths
this_dir = os.path.dirname(__file__)
vulkan_sdk_path = os.path.abspath(os.path.join(this_dir, "Vulkan-Headers"))
shader_dir = os.path.abspath(os.path.join(this_dir, "shaders"))

# Source and object files
so_sources = []
for file in os.listdir(this_dir):
    if file.endswith(".cpp") and "main" not in file:
        so_sources.append(file)
main_sources = ["motive3d.cpp", "motive2d.cpp"]
so_objects = [f"{os.path.splitext(f)[0]}.o" for f in so_sources]
main_objects = [f"{os.path.splitext(f)[0]}.o" for f in main_sources]

# Include and library paths
include_paths = [
    os.path.join(vulkan_sdk_path, "include"),
    os.path.abspath(os.path.join(this_dir, "glfw/include")),
    os.path.abspath(os.path.join(this_dir, "tinygltf")),
    os.path.abspath(os.path.join(this_dir, "glm")),
    os.path.abspath(os.path.join(this_dir, "FFmpeg/.ffmpeg/include")),
    os.path.abspath(os.path.join(this_dir, "freetype/include")),
    os.path.abspath(os.path.join(this_dir, "freetype/build/include")),
    os.path.abspath(os.path.join(this_dir, "freetype/build/include/freetype2")),
]
ffmpeg_lib_dir = os.path.abspath(os.path.join(this_dir, "FFmpeg/.ffmpeg/lib"))
lib_paths = [
    os.path.join(vulkan_sdk_path, "lib"),
    os.path.abspath(os.path.join(this_dir, "glfw/build/src")),
    ffmpeg_lib_dir,
    os.path.abspath(os.path.join(this_dir, "freetype/build")),
    os.path.abspath(os.path.join(this_dir, "FFmpeg/.ffmpeg/lib")),
    os.path.abspath(os.path.join(this_dir, ".")),
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
    # First check whether the compiler can already see the library.
    for ext in (".so", ".a"):
        result = subprocess.run(
            ["g++", f"-print-file-name=lib{lib_name}{ext}"],
            capture_output=True,
            text=True,
            check=False,
        )
        path = result.stdout.strip()
        if path and path != f"lib{lib_name}{ext}" and os.path.exists(path):
            return f"-l{lib_name}"

    # Next, look through the configured library paths explicitly.
    for base in lib_paths:
        for ext in (".a", ".so"):
            candidate = os.path.join(base, f"lib{lib_name}{ext}")
            if os.path.exists(candidate):
                return candidate

    # Finally, consult ldconfig for a versioned shared object.
    if shutil.which("ldconfig"):
        pattern = re.compile(rf"lib{re.escape(lib_name)}[\w\-]*\.so")
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
            prefix = f"lib{lib_name}"
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
print("Compiling shaders...")
for shaderFilename in os.listdir(shader_dir):
    if shaderFilename.split(".")[-1] not in ["vert", "frag", "comp"]:
        continue
    src = os.path.join(shader_dir, shaderFilename)
    dst = os.path.join(shader_dir, f"{shaderFilename}.spv")
    cmd = f"glslangValidator -V {src} -o {dst}"
    print(f"Running: {cmd}")
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"❌ Failed to compile shader: {shaderFilename}", file=sys.stderr)
        sys.exit(result.returncode)
print("✅ Shaders compiled successfully!\n")


def compile_cpp_to_o(src_file):
    obj_file = f"{os.path.splitext(src_file)[0]}.o"
    cmd = f"g++ -std=c++17 {debug_flags} {sanitize_flags} -fPIC -c {include_flags} {src_file} -o {obj_file}"
    print(f"Compiling {src_file}...")
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"❌ Failed to compile: {src_file}", file=sys.stderr)
        sys.exit(result.returncode)


all_sources = so_sources + main_sources
with ThreadPoolExecutor() as executor:
    executor.map(compile_cpp_to_o, all_sources)

# Link static library
so_link_cmd = f"ar rcs libengine.a {' '.join(so_objects)}"
print(f"\nLinking static library:\n{so_link_cmd}")
so_link_result = subprocess.run(so_link_cmd, shell=True)
if so_link_result.returncode != 0:
    print("❌ Failed to link static library.", file=sys.stderr)
    sys.exit(so_link_result.returncode)

# Link main executables
for src, obj in zip(main_sources, main_objects):
    binary_name = os.path.splitext(src)[0]
    main_link_cmd = f"g++ {debug_flags} {sanitize_flags} {lib_flags} {obj} -L. -lengine {lib_links} -o {binary_name}"
    print(f"\nLinking executable {binary_name}:\n{main_link_cmd}")
    main_link_result = subprocess.run(main_link_cmd, shell=True)
    if main_link_result.returncode != 0:
        print(f"❌ Failed to link executable {binary_name}.", file=sys.stderr)
        sys.exit(main_link_result.returncode)

print("\n✅ Build successful!")
