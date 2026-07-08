# Build the native Switch runtime

This branch compiles the Switch frontend with direct Deko3D rendering and native AArch64 execution.
It does not request custom CPU, GPU or memory clock rates.

Configure from a clean build directory:

```bash
rm -rf build-switch-native

cmake -S . -B build-switch-native \
  -DCMAKE_TOOLCHAIN_FILE="$DEVKITPRO/cmake/Switch.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SWITCH_FRONTEND=ON \
  -DENABLE_QT=OFF \
  -DENABLE_OPENGL=OFF \
  -DENABLE_VULKAN=OFF \
  -DENABLE_WEB_SERVICE=OFF \
  -DENABLE_SCRIPTING=OFF \
  -DENABLE_GDBSTUB=OFF \
  -DENABLE_TESTS=OFF \
  -DCITRA_USE_PRECOMPILED_HEADERS=OFF \
  -DENABLE_LTO=OFF \
  -DAZAHAR_DEKO3D_VALIDATION=ON \
  -DAZAHAR_DEKO3D_VERBOSE_TELEMETRY=OFF \
  -DCMAKE_PROJECT_INCLUDE="$PWD/native_runtime/NativeRuntimeFinal.cmake"

cmake --build build-switch-native --target azahar_switch -- -j2
```

Output:

```text
build-switch-native/bin/Release/azahar.nro
```

The runtime configuration enables:

- Dynarmic AArch64 safe optimizations.
- PICA AArch64 vertex-shader JIT.
- Direct transformed-triangle submission to Deko3D.
- Native Deko3D color, depth and stencil targets.
- Persistent GPU texture and render-target caches.
- Six-stage PICA TEV fragment processing.
- Native display-transfer, scaling, flip and memory-fill paths.
- Batched queue submission with reusable fenced contexts.
- Direct GPU composition into the Switch swapchain.

The Switch build does not instantiate or invoke the CPU PICA triangle rasterizer. A native state that
cannot be submitted is skipped and reported instead of being rendered by the software backend.
