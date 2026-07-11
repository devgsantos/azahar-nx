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

# Performance builds keep validation and high-volume telemetry disabled by default. They can still
# be re-enabled explicitly for a diagnostic build without editing this script.
DEKO3D_VALIDATION="${DEKO3D_VALIDATION:-OFF}"
DEKO3D_VERBOSE_TELEMETRY="${DEKO3D_VERBOSE_TELEMETRY:-OFF}"
SWITCH_PERF_DIAGNOSTICS="${SWITCH_PERF_DIAGNOSTICS:-OFF}"
SWITCH_TRACE_ENABLED="${SWITCH_TRACE_ENABLED:-OFF}"
# Partial CodeMemory publication is experimental. DKCR exited as newly published Dynarmic blocks
# began executing with this mode enabled, so stable builds retain the full libnx executable
# transition. Enable it only for isolated hardware diagnostics.
JIT_PARTIAL_PUBLISH="${JIT_PARTIAL_PUBLISH:-OFF}"
# Each full libnx CodeMemory publication flushes the complete 8 MiB JIT allocation. Dynarmic's
# consolidated mode combines block emission and block-link updates into one safe publication,
# reducing duplicate full-cache maintenance while preserving the known-good full publish path.
JIT_CONSOLIDATED_PUBLISH="${JIT_CONSOLIDATED_PUBLISH:-ON}"

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
    -DAZAHAR_DEKO3D_VALIDATION="$DEKO3D_VALIDATION" \
    -DAZAHAR_DEKO3D_VERBOSE_TELEMETRY="$DEKO3D_VERBOSE_TELEMETRY" \
    -DAZAHAR_SWITCH_PERF_DIAGNOSTICS="$SWITCH_PERF_DIAGNOSTICS" \
    -DAZAHAR_SWITCH_TRACE_ENABLED="$SWITCH_TRACE_ENABLED" \
    -DAZAHAR_SWITCH_JIT_PARTIAL_PUBLISH="$JIT_PARTIAL_PUBLISH" \
    -DAZAHAR_SWITCH_JIT_CONSOLIDATED_PUBLISH="$JIT_CONSOLIDATED_PUBLISH" \
    "$@"

cmake --build "$BUILD_DIR" --target azahar_switch -- -j"$JOBS"

echo "Build complete. Output:"
echo "  $BUILD_DIR/bin/$BUILD_TYPE/azahar.nro"
echo "  $BUILD_DIR/bin/$BUILD_TYPE/azahar_switch.elf"
