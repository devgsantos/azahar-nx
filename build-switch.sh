#!/usr/bin/env bash
# Azahar NX — native Nintendo Switch build script
# Requires devkitPro/devkitA64 and libnx.

set -euo pipefail

: "${DEVKITPRO:=/opt/devkitpro}"

if [[ ! -d "$DEVKITPRO" ]]; then
    echo "ERROR: devkitPro not found at $DEVKITPRO" >&2
    echo "Set DEVKITPRO to the correct path and try again." >&2
    exit 1
fi

BUILD_DIR="build-switch"
BUILD_TYPE="${BUILD_TYPE:-Release}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 2)}"

cmake -S . -B "$BUILD_DIR" -U CMAKE_PROJECT_INCLUDE \
    -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
    -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
    -DENABLE_SWITCH_FRONTEND=ON \
    -DENABLE_QT=OFF \
    -DENABLE_OPENGL=OFF \
    -DENABLE_VULKAN=OFF \
    -DENABLE_WEB_SERVICE=OFF \
    -DENABLE_SCRIPTING=OFF \
    -DENABLE_GDBSTUB=OFF \
    -DENABLE_TESTS=OFF \
    -DAZAHAR_DEKO3D_VALIDATION=ON \
    "$@"

cmake --build "$BUILD_DIR" --target azahar_switch -- -j"$JOBS"

echo "Build complete. Output:"
echo "  $BUILD_DIR/bin/$BUILD_TYPE/azahar.nro"
echo "  $BUILD_DIR/bin/$BUILD_TYPE/azahar_switch.elf"
