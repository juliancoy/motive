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

def ensure_exists_or_exit(directory):
    if not Path(directory).exists():
        print(f"Missing required folder: {directory}")
        print("Please fetch all submodules with:")
        print("    git submodule update --init --recursive")
        sys.exit(1)

def is_detached_head(directory):
    """Return True if the git repo at directory is in detached HEAD state."""
    result = subprocess.run(
        ["git", "symbolic-ref", "--quiet", "HEAD"],
        cwd=directory,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode != 0

def setup_vulkan_headers():
    ensure_exists_or_exit("Vulkan-Headers")
    print("Vulkan-Headers exists, checking for updates...")
    if is_detached_head("Vulkan-Headers"):
        print("Vulkan-Headers is detached (pinned submodule commit); skipping git pull.")
        return
    run_command("git pull", cwd="Vulkan-Headers")

def setup_glfw():
    ensure_exists_or_exit("glfw")

    build_dir = Path("glfw/build")
    build_dir.mkdir(exist_ok=True)

    print("Building GLFW...")
    run_command("cmake .. -DCMAKE_BUILD_TYPE=Release", cwd=build_dir)
    run_command(f"make -j{os.cpu_count()}", cwd=build_dir)

    print("GLFW built locally in glfw/build")

def setup_tinygltf():
    ensure_exists_or_exit("tinygltf")

    obj_file = Path("tinygltf/tiny_gltf.o")
    if not obj_file.exists():
        print("Building tinygltf...")
        run_command("g++ -c -std=c++11 -fPIC tinygltf/tiny_gltf.cc -o tinygltf/tiny_gltf.o")
        run_command("ar rcs libtinygltf.a tinygltf/tiny_gltf.o")
        print("tinygltf built as static library")

def setup_glm():
    ensure_exists_or_exit("glm")
    print("GLM found (header-only library, no build required)")

def setup_ffmpeg():
    ffmpeg_dir = Path("FFmpeg")
    if ffmpeg_dir.exists():
        print("FFmpeg repository already present, pulling latest changes...")
        if is_detached_head(ffmpeg_dir):
            print("FFmpeg repository is detached; skipping git pull.")
            return
        run_command("git pull", cwd=ffmpeg_dir)
    else:
        print("Cloning FFmpeg repository...")
        run_command("git clone https://github.com/FFmpeg/FFmpeg.git FFmpeg")
        print("FFmpeg repository cloned.")

    print("Configuring and building FFmpeg with static libraries...")
    install_prefix = (ffmpeg_dir / "build").resolve()
    install_prefix.mkdir(parents=True, exist_ok=True)
    configure_cmd = (
        f"./configure --prefix={install_prefix} "
        "--enable-static --disable-shared --enable-pic "
        "--disable-programs --disable-doc "
        "--enable-gpl --enable-version3"
    )
    run_command(configure_cmd, cwd=ffmpeg_dir)

    make_cmd = f"make -j{os.cpu_count()}"
    run_command(make_cmd, cwd=ffmpeg_dir)
    run_command("make install", cwd=ffmpeg_dir)
    print(f"FFmpeg built and installed to {install_prefix} with static libraries")

def setup_freetype():
    freetype_dir = Path("freetype")
    if freetype_dir.exists():
        print("FreeType repository already present, pulling latest changes...")
        if is_detached_head(freetype_dir):
            print("FreeType repository is detached; skipping git pull.")
        else:
            run_command("git pull", cwd=freetype_dir)
    else:
        print("Cloning FreeType repository...")
        run_command("git clone https://github.com/freetype/freetype.git freetype")
        print("FreeType repository cloned.")

    build_dir = freetype_dir / "build"
    install_dir = build_dir / "install"
    build_dir.mkdir(parents=True, exist_ok=True)
    install_dir.mkdir(parents=True, exist_ok=True)

    cmake_cmd = (
        f"cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF "
        f"-DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_INSTALL_PREFIX={install_dir}"
    )
    print("Configuring FreeType...")
    run_command(cmake_cmd, cwd=build_dir)

    print("Building FreeType...")
    run_command(f"make -j{os.cpu_count()}", cwd=build_dir)
    run_command("make install", cwd=build_dir)
    print(f"FreeType built and installed to {install_dir}")

def main():
    print("=== Checking and setting up development dependencies ===")
    setup_vulkan_headers()
    setup_glfw()
    setup_tinygltf()
    setup_glm()
    setup_ffmpeg()
    setup_freetype()
    print("=== Setup complete ===")

if __name__ == "__main__":
    main()
