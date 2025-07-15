#!/usr/bin/env python3
import os
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

# Paths
this_dir = os.path.dirname(__file__)
vulkan_sdk_superpath = os.path.abspath(os.path.join(this_dir, "../Vulkan-Headers/"))
vulkan_sdk_path = vulkan_sdk_superpath
setup_script = "setup-env.sh"
original_dir = os.getcwd()
shader_dir = os.path.abspath(os.path.join(this_dir, "shaders"))

# Source and object files
so_sources =[]
for file in os.listdir(this_dir):
    if file.endswith(".cpp") and "main" not in file:
        so_sources += [file]
main_sources = ["main.cpp"]
so_objects = [f"{os.path.splitext(f)[0]}.o" for f in so_sources]
main_objects = [f"{os.path.splitext(f)[0]}.o" for f in main_sources]

# Include and library paths
include_paths = [
    os.path.join(vulkan_sdk_path, "include"),
    os.path.abspath(os.path.join(this_dir, "../glfw/include")),
    os.path.abspath(os.path.join(this_dir, "../tinygltf")),
    os.path.abspath(os.path.join(this_dir, "../glm/glm")),
]
lib_paths = [
    os.path.join(vulkan_sdk_path, "lib"),
    os.path.abspath(os.path.join(this_dir, "../glfw/build/src")),
    os.path.abspath(os.path.join(this_dir, "../"))
]
libraries = [
    "glfw3",
    "tinygltf",
    "vulkan"
]

# Flags
debug_flags = "-g -O0"
DEBUG_MODE = "NONE"
sanitize_flags = ""
if DEBUG_MODE == "ADDRESS_SANITIZER":
    sanitize_flags = "-fsanitize=address -fno-omit-frame-pointer"

include_flags = " ".join(f"-I{p}" for p in include_paths)
lib_flags = " ".join(f"-L{p}" for p in lib_paths)
lib_links = " ".join(f"-l{lib}" for lib in libraries)

# Compile shaders
print("Compiling shaders...")
for shaderFilename in os.listdir(shader_dir):
    if shaderFilename.split(".")[-1] not in ["vert", "frag"]:
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

# Function to compile one file
def compile_cpp_to_o(src_file):
    obj_file = f"{os.path.splitext(src_file)[0]}.o"
    cmd = f"g++ -std=c++17 {debug_flags} {sanitize_flags} -fPIC -c {include_flags} {src_file} -o {obj_file}"
    print(f"Compiling {src_file}...")
    result = subprocess.run(cmd, shell=True)
    if result.returncode != 0:
        print(f"❌ Failed to compile: {src_file}", file=sys.stderr)
        sys.exit(result.returncode)

# Compile source files in parallel
all_sources = so_sources + main_sources
with ThreadPoolExecutor() as executor:
    executor.map(compile_cpp_to_o, all_sources)

# Link shared object
so_link_cmd = f"g++ -shared {sanitize_flags} {lib_flags} {' '.join(so_objects)} {lib_links} -o libengine.so"
print(f"\nLinking shared library:\n{so_link_cmd}")
so_link_result = subprocess.run(so_link_cmd, shell=True)
if so_link_result.returncode != 0:
    print("❌ Failed to link shared library.", file=sys.stderr)
    sys.exit(so_link_result.returncode)

# Link main executable
main_link_cmd = f"g++ {debug_flags} {sanitize_flags} {lib_flags} {' '.join(main_objects)} -L. -lengine {lib_links} -o main"
print(f"\nLinking main executable:\n{main_link_cmd}")
main_link_result = subprocess.run(main_link_cmd, shell=True)
if main_link_result.returncode != 0:
    print("❌ Failed to link executable.", file=sys.stderr)
    sys.exit(main_link_result.returncode)

print("\n✅ Build successful!")
print("\nDebugging tips:")
print("1. Run with GDB: gdb -ex run -ex bt --args ./main")
print("2. For better backtraces: gdb -ex 'set pagination off' -ex run -ex bt -ex quit --args ./main")
print("3. To examine core dumps (if enabled):")
print("   - First enable core dumps: ulimit -c unlimited")
print("   - After crash: gdb ./main core")
