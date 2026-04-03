#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
CMAKE_ARGS=()
BUILD_ARGS=()

while [[ $# -gt 0 ]]; do
    case "$1" in
        --asan)
            BUILD_DIR="${ROOT_DIR}/build-asan"
            CMAKE_ARGS+=("-DMOTIVE_ENABLE_ASAN=ON")
            shift
            ;;
        *)
            BUILD_ARGS+=("$1")
            shift
            ;;
    esac
done

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" "${CMAKE_ARGS[@]}"
cmake --build "${BUILD_DIR}" "${BUILD_ARGS[@]}"
