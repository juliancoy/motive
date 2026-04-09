#!/usr/bin/env bash

# Motive 3D Engine Windows build script
# Usage: ./build_windows.sh [options]

set -euo pipefail

BUILD_TYPE="Release"
ENABLE_ASAN="OFF"
ENABLE_VALIDATION="ON"
CLEAN_BUILD="OFF"
FULL_BUILD="OFF"
VERBOSE="OFF"
GENERATOR=""
ARCH="x64"
TOOLSET=""

if command -v nproc >/dev/null 2>&1; then
    JOBS="$(nproc)"
else
    JOBS="${NUMBER_OF_PROCESSORS:-8}"
fi

print_help() {
    cat <<'EOF'
Motive 3D Engine Windows Build Script

Usage: ./build_windows.sh [options]

Options:
  --asan              Enable AddressSanitizer (sets Debug build)
  --no-validation     Disable Vulkan validation layers
  --clean             Remove build-windows before configuring
  --full              Build motive3d, motive2d, encode, and motive_editor
  --jobs N            Number of parallel jobs
  --generator NAME    CMake generator (default: Ninja, else Visual Studio 17 2022)
  --arch NAME         Visual Studio architecture (default: x64)
  --toolset NAME      Visual Studio toolset (example: clangcl)
  --verbose           Verbose build output
  --help, -h          Show this help message
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --asan)
            ENABLE_ASAN="ON"
            BUILD_TYPE="Debug"
            shift
            ;;
        --no-validation)
            ENABLE_VALIDATION="OFF"
            shift
            ;;
        --clean)
            CLEAN_BUILD="ON"
            shift
            ;;
        --full)
            FULL_BUILD="ON"
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --generator)
            GENERATOR="$2"
            shift 2
            ;;
        --arch)
            ARCH="$2"
            shift 2
            ;;
        --toolset)
            TOOLSET="$2"
            shift 2
            ;;
        --verbose)
            VERBOSE="ON"
            shift
            ;;
        --help|-h)
            print_help
            exit 0
            ;;
        *)
            echo "Unknown option: $1"
            echo "Run ./build_windows.sh --help for usage."
            exit 1
            ;;
    esac
done

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build-windows"

if ! command -v cmake >/dev/null 2>&1; then
    echo "CMake is required but was not found in PATH."
    exit 1
fi

if [[ -z "${GENERATOR}" ]]; then
    if command -v ninja >/dev/null 2>&1; then
        GENERATOR="Ninja"
    else
        GENERATOR="Visual Studio 17 2022"
    fi
fi

IS_MULTI_CONFIG="OFF"
if [[ "${GENERATOR}" == Visual\ Studio* ]] || [[ "${GENERATOR}" == *"Multi-Config"* ]]; then
    IS_MULTI_CONFIG="ON"
fi

if [[ "${CLEAN_BUILD}" == "ON" ]]; then
    echo "Cleaning ${BUILD_DIR}"
    rm -rf "${BUILD_DIR}"
fi

mkdir -p "${BUILD_DIR}"

echo "Motive Windows build"
echo "  Generator:   ${GENERATOR}"
echo "  Build type:  ${BUILD_TYPE}"
echo "  ASan:        ${ENABLE_ASAN}"
echo "  Validation:  ${ENABLE_VALIDATION}"
echo "  Full build:  ${FULL_BUILD}"
echo "  Jobs:        ${JOBS}"

if [[ "${ENABLE_ASAN}" == "ON" && "${GENERATOR}" == Visual\ Studio* && -z "${TOOLSET}" ]]; then
    echo "Warning: ASan with default MSVC toolset may fail. Use --toolset clangcl if needed."
fi

CONFIGURE_ARGS=(
    -S "${SCRIPT_DIR}"
    -B "${BUILD_DIR}"
    -G "${GENERATOR}"
    -DMOTIVE_ENABLE_ASAN="${ENABLE_ASAN}"
    -DMOTIVE_ENABLE_VALIDATION="${ENABLE_VALIDATION}"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [[ "${IS_MULTI_CONFIG}" == "OFF" ]]; then
    CONFIGURE_ARGS+=(-DCMAKE_BUILD_TYPE="${BUILD_TYPE}")
fi

if [[ "${GENERATOR}" == Visual\ Studio* ]]; then
    CONFIGURE_ARGS+=(-A "${ARCH}")
fi

if [[ -n "${TOOLSET}" ]]; then
    CONFIGURE_ARGS+=(-T "${TOOLSET}")
fi

cmake "${CONFIGURE_ARGS[@]}"

BUILD_ARGS=(
    --build "${BUILD_DIR}"
    --parallel "${JOBS}"
)

if [[ "${IS_MULTI_CONFIG}" == "ON" ]]; then
    BUILD_ARGS+=(--config "${BUILD_TYPE}")
fi

if [[ "${FULL_BUILD}" == "ON" ]]; then
    BUILD_ARGS+=(--target motive3d motive2d encode motive_editor)
else
    BUILD_ARGS+=(--target motive_editor)
fi

if [[ "${VERBOSE}" == "ON" ]]; then
    BUILD_ARGS+=(--verbose)
fi

cmake "${BUILD_ARGS[@]}"

echo "Build complete."
