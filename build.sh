#!/bin/bash

# Motive 3D Engine Build Script
# Usage: ./build.sh [options]
#   --asan          Enable AddressSanitizer
#   --no-validation Disable Vulkan validation layers
#   --clean         Clean build directory first
#   --full          Build additional executables (motive3d_runtime, motive2d, encode)
#   --test-ux       Build and run runtime UX integration tests (requires motive_editor)
#   --check-cpp-size Run first-party .cpp size checks (warn-only unless --fail-cpp-size is also set)
#   --fail-cpp-size  Fail build if first-party .cpp size checks find violations
#   --jobs N        Number of parallel jobs (default: auto)
#   --verbose       Verbose build output

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Default options
BUILD_TYPE="Release"
ENABLE_ASAN=OFF
ENABLE_VALIDATION=ON
CLEAN_BUILD=OFF
VERBOSE=OFF
FULL_BUILD=OFF
RUN_UX_TESTS=OFF
CHECK_CPP_SIZE=OFF
FAIL_CPP_SIZE=OFF
JOBS=$(nproc 2>/dev/null || echo 4)

# Parse arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --asan)
            ENABLE_ASAN=ON
            BUILD_TYPE="Debug"
            echo -e "${YELLOW}⚠️  AddressSanitizer enabled (Debug build)${NC}"
            shift
            ;;
        --no-validation)
            ENABLE_VALIDATION=OFF
            echo -e "${BLUE}ℹ️  Vulkan validation layers disabled${NC}"
            shift
            ;;
        --clean)
            CLEAN_BUILD=ON
            echo -e "${YELLOW}🧹 Clean build requested${NC}"
            shift
            ;;
        --full)
            FULL_BUILD=ON
            echo -e "${BLUE}ℹ️  Full build enabled (runtime executables will be built)${NC}"
            shift
            ;;
        --test-ux)
            RUN_UX_TESTS=ON
            echo -e "${BLUE}ℹ️  Runtime UX tests enabled${NC}"
            shift
            ;;
        --check-cpp-size)
            CHECK_CPP_SIZE=ON
            echo -e "${BLUE}ℹ️  C++ file-size check enabled${NC}"
            shift
            ;;
        --fail-cpp-size)
            CHECK_CPP_SIZE=ON
            FAIL_CPP_SIZE=ON
            echo -e "${YELLOW}⚠️  C++ file-size check will fail on violations${NC}"
            shift
            ;;
        --jobs)
            JOBS="$2"
            shift 2
            ;;
        --verbose)
            VERBOSE=ON
            shift
            ;;
        --help|-h)
            echo "Motive 3D Engine Build Script"
            echo ""
            echo "Usage: ./build.sh [options]"
            echo ""
            echo "Options:"
            echo "  --asan          Enable AddressSanitizer (Debug build)"
            echo "  --no-validation Disable Vulkan validation layers"
            echo "  --clean         Clean build directory first"
            echo "  --full          Build motive3d, motive3d_runtime, motive2d, and encode"
            echo "  --test-ux       Build and run runtime UX integration tests via ctest"
            echo "  --check-cpp-size Run first-party C++ file-size check (warn-only)"
            echo "  --fail-cpp-size  Fail on first-party C++ file-size violations"
            echo "  --jobs N        Number of parallel jobs (default: auto)"
            echo "  --verbose       Verbose build output"
            echo "  --help, -h      Show this help message"
            echo ""
            exit 0
            ;;
        *)
            echo -e "${RED}❌ Unknown option: $1${NC}"
            echo "Run './build.sh --help' for usage information"
            exit 1
            ;;
    esac
done

# Directories
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"

echo -e "${BLUE}🔨 Motive 3D Engine Build${NC}"
echo "================================"
echo "Build type:     $BUILD_TYPE"
echo "ASan:           $ENABLE_ASAN"
echo "Validation:     $ENABLE_VALIDATION"
echo "Full build:     $FULL_BUILD"
echo "UX tests:       $RUN_UX_TESTS"
echo "C++ size check: $CHECK_CPP_SIZE"
echo "Parallel jobs:  $JOBS"
echo ""

# Check dependencies
echo -e "${BLUE}📋 Checking dependencies...${NC}"

# Check for cmake
if ! command -v cmake &> /dev/null; then
    echo -e "${RED}❌ CMake not found. Please install CMake 3.22+${NC}"
    exit 1
fi

CMAKE_VERSION=$(cmake --version | head -1 | grep -oE '[0-9]+\.[0-9]+' | head -1)
echo "  CMake version: $CMAKE_VERSION"

# Check for Qt6
if ! command -v qtpaths6 &> /dev/null && ! pkg-config --exists Qt6Widgets 2>/dev/null; then
    echo -e "${YELLOW}⚠️  Qt6 not found in PATH. Make sure Qt6 is installed.${NC}"
fi

# Clean build if requested
if [ "$CLEAN_BUILD" = "ON" ]; then
    echo -e "${YELLOW}🧹 Cleaning build directory...${NC}"
    rm -rf "${BUILD_DIR}"
fi

# Create build directory
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

# Configure
echo ""
echo -e "${BLUE}⚙️  Configuring...${NC}"
CMAKE_ARGS=(
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE"
    -DMOTIVE_ENABLE_ASAN="$ENABLE_ASAN"
    -DMOTIVE_ENABLE_VALIDATION="$ENABLE_VALIDATION"
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
)

if [ "$RUN_UX_TESTS" = "ON" ]; then
    CMAKE_ARGS+=(-DMOTIVE_ENABLE_TESTS=ON)
fi

if [ "$VERBOSE" = "ON" ]; then
    CMAKE_ARGS+=(--verbose)
fi

if ! cmake "${SCRIPT_DIR}" "${CMAKE_ARGS[@]}"; then
    echo -e "${RED}❌ Configuration failed${NC}"
    exit 1
fi

# Build
echo ""
echo -e "${BLUE}🔧 Building...${NC}"
BUILD_ARGS=(--parallel "$JOBS")
if [ "$FULL_BUILD" = "ON" ]; then
    BUILD_ARGS+=(--target motive3d motive3d_runtime motive2d encode)
elif [ "$RUN_UX_TESTS" = "ON" ]; then
    BUILD_ARGS+=(--target motive3d motive_editor motive_tests text_oblique_png text_extrusion_png)
else
    BUILD_ARGS+=(--target motive3d)
fi

if [ "$VERBOSE" = "ON" ]; then
    BUILD_ARGS+=(--verbose)
fi

if ! cmake --build . "${BUILD_ARGS[@]}"; then
    echo -e "${RED}❌ Build failed${NC}"
    exit 1
fi

# Success
echo ""
echo -e "${GREEN}✅ Build successful!${NC}"
echo ""
echo "Built targets:"
if [ "$FULL_BUILD" = "ON" ]; then
    TARGETS=(motive3d motive3d_runtime motive2d encode)
elif [ "$RUN_UX_TESTS" = "ON" ]; then
    TARGETS=(motive3d motive_editor motive_tests text_oblique_png text_extrusion_png)
else
    TARGETS=(motive3d)
fi
for f in "${TARGETS[@]}"; do
    if [ -f "${BUILD_DIR}/$f" ]; then
        size=$(du -h "${BUILD_DIR}/$f" | cut -f1)
        echo "  $f ($size)"
    fi
done

if [ "$RUN_UX_TESTS" = "ON" ]; then
    echo ""
    echo -e "${BLUE}🧪 Running runtime UX tests...${NC}"
    if ! ctest --output-on-failure -L ux; then
        echo -e "${RED}❌ Runtime UX tests failed${NC}"
        exit 1
    fi
    echo -e "${GREEN}✅ Runtime UX tests passed${NC}"
fi

if [ "$CHECK_CPP_SIZE" = "ON" ]; then
    echo ""
    echo -e "${BLUE}📏 Checking first-party C++ file sizes...${NC}"
    SIZE_ARGS=(
        --root "${SCRIPT_DIR}"
        --max-lines 1500
        --exclude-dir build
        --exclude-dir FFmpeg
        --exclude-dir Vulkan-Video-Samples
        --exclude-dir Vulkan-Headers
        --exclude-dir VulkanMemoryAllocator
        --exclude-dir bullet3
        --exclude-dir glm
        --exclude-dir glfw
        --exclude-dir imgui
        --exclude-dir jolt
        --exclude-dir ncnn
        --exclude-dir tinygltf
        --exclude-dir ufbx
        --exclude-dir common_vv
        --exclude-dir vk_video_decoder
    )
    if [ "$FAIL_CPP_SIZE" = "ON" ]; then
        SIZE_ARGS+=(--fail)
    fi
    if ! python "${SCRIPT_DIR}/tools_check_cpp_size.py" "${SIZE_ARGS[@]}"; then
        echo -e "${RED}❌ C++ file-size check failed${NC}"
        exit 1
    fi
fi

echo ""
if [ "$FULL_BUILD" = "ON" ]; then
    echo -e "${GREEN}🎉 All done! Run ./motive3d to start the application.${NC}"
    echo ""
    echo "Shell app:"
    echo "  ./motive3d"
    echo ""
    echo "Standalone runtime options:"
    echo "  ./motive3d_runtime --parallel    Enable parallel scene loading"
    echo "  ./motive3d_runtime --help        Show all runtime options"
else
    echo -e "${GREEN}🎉 All done! motive3d is ready.${NC}"
    echo ""
    echo "Use ./build.sh --full if you also want motive3d_runtime, motive2d, and encode."
fi
