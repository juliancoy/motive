#!/bin/bash

# Motive 3D Engine Build Script
# Usage: ./build.sh [options]
#   --asan          Enable AddressSanitizer
#   --no-validation Disable Vulkan validation layers
#   --clean         Clean build directory first
#   --full          Build additional executables (motive3d_runtime, motive2d, encode)
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
else
    TARGETS=(motive3d)
fi
for f in "${TARGETS[@]}"; do
    if [ -f "${BUILD_DIR}/$f" ]; then
        size=$(du -h "${BUILD_DIR}/$f" | cut -f1)
        echo "  $f ($size)"
    fi
done

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
