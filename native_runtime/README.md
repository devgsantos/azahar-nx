# Native Deko3D runtime test

This branch activates a strict Nintendo Switch-native execution path without CPU PICA triangle
rasterization.

## Configure

Use the same Nintendo Switch toolchain and options as the normal project build, adding:

```bash
-DCMAKE_PROJECT_INCLUDE="$PWD/native_runtime/NativeRuntime.cmake"
```

Example when configuring from the repository root:

```bash
cmake -S . -B build-switch-native \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SWITCH_FRONTEND=ON \
  -DAZAHAR_DEKO3D_VALIDATION=ON \
  -DAZAHAR_DEKO3D_VERBOSE_TELEMETRY=OFF \
  -DCMAKE_PROJECT_INCLUDE="$PWD/native_runtime/NativeRuntime.cmake" \
  <the existing Nintendo Switch toolchain arguments>

cmake --build build-switch-native -j"$(nproc)"
```

## Runtime policy

- Dynarmic safe optimizations are enabled for the Switch CPU path.
- The Deko3D queue keeps graphics, compute and Z-cull capabilities.
- Cached DkImage and DkImageView objects use stable storage and views are rebound after insertion.
- GPU memory is not destroyed while queued commands may still reference it.
- The most recent GPU-authored guest color target is selected for native presentation.
- CPU PICA triangle rasterization is replaced with a no-op safety sink.
- Common transformed draws with textures, alpha test, stencil, logic-op or depth-write state are
  allowed through the native Deko3D path. Those details are not all faithfully translated yet, so
  missing textures and visual glitches are expected in this test.
- Depth-test-only draws remain skipped until the native depth binding is valid for every state.

Test Donkey Kong Country Returns 3D first because it reaches the transformed PICA draw path. Blank
output, a validation error, or a crash should be captured from process start through the first
failure.
