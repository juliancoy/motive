#!/usr/bin/env python3
import os
import subprocess
import sys
from pathlib import Path

def run_command(cmd, cwd=None):
    try:
        subprocess.run(cmd, check=True, shell=True, cwd=cwd)
    except subprocess.CalledProcessError:
        print(f"Command failed: {cmd}", file=sys.stderr)
        sys.exit(1)

def setup_vulkan_headers():
    vulkan_headers_dir = Path("Vulkan-Headers")
    if not vulkan_headers_dir.exists():
        print("Cloning Vulkan-Headers...")
        run_command("git clone https://github.com/KhronosGroup/Vulkan-Headers.git")
    else:
        print("Vulkan-Headers already exists, pulling latest changes...")
        run_command("git pull", cwd=str(vulkan_headers_dir))

def setup_glfw():
    glfw_dir = Path("glfw")

    if not glfw_dir.exists():
        print("Cloning GLFW...")
        run_command("git clone https://github.com/glfw/glfw.git")

    build_dir = glfw_dir / "build"
    build_dir.mkdir(exist_ok=True)

    print("Building GLFW...")
    run_command("cmake .. -DCMAKE_BUILD_TYPE=Release", cwd=build_dir)
    run_command(f"make -j{os.cpu_count()}", cwd=build_dir)

    print("GLFW built locally in glfw/build")

def setup_tinygltf():
    tinygltf_dir = Path("tinygltf")
    if not tinygltf_dir.exists():
        print("Cloning tinygltf...")
        run_command("git clone https://github.com/syoyo/tinygltf.git")

        print("Building tinygltf...")
        run_command("g++ -c -std=c++11 -fPIC tinygltf/tiny_gltf.cc -o tinygltf/tiny_gltf.o")
        run_command("ar rcs libtinygltf.a tinygltf/tiny_gltf.o")
        print("tinygltf built as static library")

def setup_glm():
    glm_dir = Path("glm")
    if not glm_dir.exists():
        print("Cloning GLM...")
        run_command("git clone https://github.com/g-truc/glm.git")
        print("GLM downloaded (header-only library, no build required)")

def main():
    print("=== Setting up development dependencies ===")
    setup_vulkan_headers()
    setup_glfw()
    setup_tinygltf()
    setup_glm()
    print("=== Setup complete ===")

if __name__ == "__main__":
    main()
