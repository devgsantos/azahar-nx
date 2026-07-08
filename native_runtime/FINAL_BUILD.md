# Build this branch

Use your normal Nintendo Switch CMake toolchain arguments and add:

```bash
-DCMAKE_PROJECT_INCLUDE="$PWD/native_runtime/NativeRuntimeFinal.cmake"
```

Example:

```bash
cmake -S . -B build-switch-native \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SWITCH_FRONTEND=ON \
  -DAZAHAR_DEKO3D_VALIDATION=ON \
  -DAZAHAR_DEKO3D_VERBOSE_TELEMETRY=OFF \
  -DCMAKE_PROJECT_INCLUDE="$PWD/native_runtime/NativeRuntimeFinal.cmake" \
  <your existing Nintendo Switch toolchain arguments>

cmake --build build-switch-native -j"$(nproc)"
```

The final entrypoint enables Dynarmic safe optimizations, strict native Deko3D drawing, stable
GPU-surface ownership, full graphics/compute/Z-cull queue flags, deferred GPU-resource destruction,
and direct presentation of the latest GPU-authored guest color target.
