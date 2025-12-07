#!/usr/bin/env python3
import os
import shutil
import subprocess
import sys
import re
from pathlib import Path
import textwrap


def pkg_config_exists(package):
    """Return True if pkg-config can resolve the requested package."""
    result = subprocess.run(
        ["pkg-config", "--exists", package],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return result.returncode == 0

def pkg_config_version(package):
    """Return the pkg-config reported version for the package, or None."""
    result = subprocess.run(
        ["pkg-config", "--modversion", package],
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return None
    return result.stdout.strip()

def version_gte(found, required):
    """Compare dotted numeric version strings."""
    def split(v):
        return [int(x) for x in re.split(r"[^\d]", v) if x]
    f_parts = split(found)
    r_parts = split(required)
    length = max(len(f_parts), len(r_parts))
    f_parts.extend([0] * (length - len(f_parts)))
    r_parts.extend([0] * (length - len(r_parts)))
    return f_parts >= r_parts

def ensure_local_vulkan_pc():
    """
    Create a pkg-config file for the Vulkan-Headers submodule so FFmpeg can pick
    up a sufficiently new Vulkan header version even if the system version is older.
    """
    header = Path("Vulkan-Headers/include/vulkan/vulkan_core.h")
    ensure_exists_or_exit(header)

    header_text = header.read_text()
    version_match = re.search(
        r"VK_MAKE_API_VERSION\(\s*0\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*VK_HEADER_VERSION\s*\)",
        header_text,
    )
    header_version_match = re.search(r"#define\s+VK_HEADER_VERSION\s+(\d+)", header_text)
    if not (version_match and header_version_match):
        print("Could not parse Vulkan header version from Vulkan-Headers.")
        sys.exit(1)

    major, minor = version_match.groups()
    header_version = header_version_match.group(1)
    full_version = f"{major}.{minor}.{header_version}"

    pc_dir = Path("Vulkan-Headers/.pkgconfig")
    pc_dir.mkdir(parents=True, exist_ok=True)
    pc_path = pc_dir / "vulkan.pc"

    prefix = Path("Vulkan-Headers").resolve()
    contents = textwrap.dedent(
        f"""\
        prefix={prefix}
        includedir=${{prefix}}/include

        Name: vulkan
        Description: Vulkan Headers (local)
        Version: {full_version}
        Cflags: -I${{includedir}}
        Libs:
        """
    )
    pc_path.write_text(contents)
    return pc_dir

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
    cmake_flags = ["-DCMAKE_BUILD_TYPE=Release"]

    have_wayland = pkg_config_exists("wayland-client")
    have_x11 = all(pkg_config_exists(pkg) for pkg in ["x11", "xcursor", "xrandr", "xi"])

    if not have_wayland and not have_x11:
        print("Neither Wayland nor X11 development headers were found.")
        print("Attempting to install X11 dev headers via apt-get...")
        x11_packages = "libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev"
        if shutil.which("apt-get"):
            run_command(f"sudo apt-get update && sudo apt-get install -y {x11_packages}")
            have_x11 = all(pkg_config_exists(pkg) for pkg in ["x11", "xcursor", "xrandr", "xi"])
        else:
            print("apt-get not available; cannot auto-install X11 dependencies.")

        if not have_x11:
            print("Install at least one set of dev packages and rerun build_deps.py.")
            print("For X11 on Ubuntu: sudo apt-get install libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev")
            print("For Wayland on Ubuntu: sudo apt-get install libwayland-dev libxkbcommon-dev wayland-protocols")
            sys.exit(1)

    # Ubuntu 24.04 desktop images often lack Wayland dev headers by default.
    # If wayland-client is missing, disable Wayland so the build can proceed
    # with the X11 path only.
    if not have_wayland:
        print("wayland-client dev package not found; building GLFW with Wayland disabled.")
        print("To enable Wayland, install: libwayland-dev libxkbcommon-dev wayland-protocols")
        cmake_flags.append("-DGLFW_BUILD_WAYLAND=OFF")

    # If X11 (including Xcursor) headers are missing but Wayland is present,
    # build GLFW with Wayland only to avoid missing Xcursor headers.
    if have_wayland and not have_x11:
        print("X11/Xcursor dev packages not found; building GLFW with X11 disabled (Wayland only).")
        print("To enable X11, install: libx11-dev libxcursor-dev libxrandr-dev libxinerama-dev libxi-dev")
        cmake_flags.append("-DGLFW_BUILD_X11=OFF")

    cmake_cmd = "cmake .. " + " ".join(cmake_flags)
    run_command(cmake_cmd, cwd=build_dir)
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

def setup_ffnvcodec():
    install_dir = Path("/usr/include/ffnvcodec")
    if install_dir.exists():
        print("nv-codec-headers already installed in /usr/include/ffnvcodec; skipping.")
        return
    repo_dir = Path("nv-codec-headers")
    if not repo_dir.exists():
        print("Cloning nv-codec-headers...")
        run_command("git clone https://github.com/FFmpeg/nv-codec-headers.git nv-codec-headers")
    print("Building nv-codec-headers...")
    run_command("make", cwd=repo_dir)
    print("Installing nv-codec-headers (requires sudo)...")
    run_command("sudo make install", cwd=repo_dir)

def cuda_available():
    """Simple detection for CUDA toolkit availability."""
    if shutil.which("nvcc"):
        return True
    cuda_env = os.environ.get("CUDA_HOME") or os.environ.get("CUDA_PATH")
    if cuda_env and Path(cuda_env).exists():
        return True
    default_path = Path("/usr/local/cuda")
    return default_path.exists()


def ffnvcodec_available():
    """Detect whether the NVIDIA ffnvcodec headers are installed."""
    custom_path = os.environ.get("FFNV_CODEC_INCLUDE")
    candidates = []
    if custom_path:
        candidates.append(Path(custom_path))
    candidates.extend(
        [
            Path("/usr/include/ffnvcodec"),
            Path("/usr/local/include/ffnvcodec"),
            Path.home() / ".local/include/ffnvcodec",
        ]
    )

    for directory in candidates:
        if (directory / "nvEncodeAPI.h").exists() or (directory / "nvDecodeAPI.h").exists():
            return True
    return False


def determine_hwaccel_flags():
    """Return configure flags for FFmpeg hardware decode features the system supports."""
    pkg_features = [
        ("--enable-libdrm", ["libdrm"], "libdrm"),
        ("--enable-vaapi", ["libva"], "VAAPI"),
        ("--enable-vdpau", ["vdpau"], "VDPAU"),
        ("--enable-opencl", ["OpenCL"], "OpenCL"),
    ]
    enabled = []
    skipped = []
    flags = []

    for flag, packages, description in pkg_features:
        if all(pkg_config_exists(pkg) for pkg in packages):
            flags.append(flag)
            enabled.append(description)
        else:
            skipped.append(description)

    # Vulkan: ffmpeg master currently requires headers >= 1.3.277
    # Disabling Vulkan support due to header issues
    vulkan_version = pkg_config_version("vulkan")
    vulkan_required = "1.3.277"
    # Always skip Vulkan for now
    msg = f"Vulkan (requires >= {vulkan_required}"
    if vulkan_version:
        msg += f", found {vulkan_version})"
    else:
        msg += ", not found)"
    msg += " - disabled due to header issues"
    skipped.append(msg)

    cuda_flags = ["--enable-cuda", "--enable-cuvid", "--enable-nvdec", "--enable-nvenc"]
    if cuda_available() and ffnvcodec_available():
        flags.extend(cuda_flags)
        enabled.append("CUDA/NVDEC")
    else:
        skipped.append("CUDA/NVDEC (requires CUDA toolkit + ffnvcodec headers)")

    return {"flags": flags, "enabled_descriptions": enabled, "skipped_descriptions": skipped}

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

    # Ensure nasm exists for x86 assembly optimizations; otherwise, builds will fail.
    if shutil.which("nasm") is None:
        print("nasm assembler not found. Attempting to install via apt-get...")
        if shutil.which("apt-get"):
            run_command("sudo apt-get update && sudo apt-get install -y nasm")
        else:
            print("apt-get not available; please install nasm manually and re-run.")
            sys.exit(1)

    # Prefer the in-tree Vulkan-Headers by exporting a pkg-config file for them.
    local_vulkan_pc_dir = ensure_local_vulkan_pc()
    # Use absolute path so it works regardless of current working directory
    local_vulkan_pc_dir_abs = local_vulkan_pc_dir.resolve()
    existing_pc_path = os.environ.get("PKG_CONFIG_PATH", "")
    pc_paths = [str(local_vulkan_pc_dir_abs)]
    if existing_pc_path:
        pc_paths.append(existing_pc_path)
    pc_env = ":".join(pc_paths)
    os.environ["PKG_CONFIG_PATH"] = pc_env

    print("Configuring and building FFmpeg with static libraries...")
    build_dir = ffmpeg_dir / ".build"
    build_dir.mkdir(parents=True, exist_ok=True)
    install_prefix = (ffmpeg_dir / ".ffmpeg").resolve()
    install_prefix.mkdir(parents=True, exist_ok=True)
    hwaccel_options = determine_hwaccel_flags()
    if hwaccel_options["flags"]:
        print("Enabling FFmpeg hardware decode features:", ", ".join(hwaccel_options["enabled_descriptions"]))
    else:
        print("No optional FFmpeg hardware decode features detected.")
    if hwaccel_options["skipped_descriptions"]:
        print("Skipping unavailable hardware features:", ", ".join(hwaccel_options["skipped_descriptions"]))
    configure_cmd = (
        f"PKG_CONFIG_PATH={pc_env} ../configure --prefix={install_prefix} "
        "--enable-static --disable-shared --enable-pic "
        "--disable-programs --disable-doc "
        "--enable-gpl --enable-version3 "
        + " ".join(hwaccel_options.get("flags", []))
    )
    run_command(configure_cmd, cwd=build_dir)

    make_cmd = f"make -j{os.cpu_count()}"
    run_command(make_cmd, cwd=build_dir)
    run_command("make install", cwd=build_dir)
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
    setup_ffnvcodec()
    setup_ffmpeg()
    setup_freetype()
    print("=== Setup complete ===")

if __name__ == "__main__":
    main()
