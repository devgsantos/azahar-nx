# Azahar NX — Full Switch Implementation Plan

Goal: make 3DS games playable on Nintendo Switch at stable FPS, with native CPU JIT,
full Deko3D GPU rendering, and functional audio/input.

This document is a living roadmap. Each phase should be implemented, tested on
hardware, and measured before moving to the next.

---

## 1. Current State (as of this review)

### What works
- Native Switch NRO build (`azahar_switch`) with devkitPro/libnx.
- ROM browser, fixed SD layout (`sdmc:/switch/azahar/`), file logging.
- Joy-Con digital input mapped.
- `audren` initialized but **not connected** to the emulator audio pipeline.
- Dynarmic ARM JIT using Horizon `jitCreate` dual-alias memory.
- Deko3D device/queue/swapchain/framebuffer creation.
- A very limited hardware triangle path (untextured, RGBA8, no depth, no blend
  mismatch, triangle lists only) plus a software-rasterizer fallback.
- CPU readback of guest framebuffers + GPU upload for presentation.
- Offline network/service stubs so single-player titles can boot.

### Critical gaps
- Deko3D rasterizer falls back to software for: textures, depth/stencil, alpha
  test, most blend/logic-op combos, non-RGBA8 formats, geometry shaders,
  shadow rendering, and indexed/direct draws.
- Shaders are fixed vertex-color only; there is no PICA shader translation.
- Texture cache is a stub (`deko3d_texture_cache`).
- Present path copies the full 400x240/320x240 frames from guest VRAM every
  frame instead of GPU blitting render targets.
- Audio output is `NullSink`; `audren` is unused for samples.
- Input is digital-only; analog stick values and touch screen are ignored.
- Dynarmic runs with `no_optimizations` by default and full JIT region
  publish/unpublish on every block, which is expensive.

---

## 2. Guiding Principles

- **Minimal upstream fixes first.** Prefer fixing the real bottleneck instead of
  adding more fallback wrappers.
- **Measure before and after.** Use the existing `deko3d_stats` telemetry and
  the per-second frontend log as the primary feedback loop.
- **Feature parity with the OpenGL/Vulkan renderers is the target.** The
  Deko3D renderer must eventually cover the same PICA feature set.
- **Do not break the software fallback until the hardware path is proven.**
  Keep games booting and rendering while hardware coverage grows.
- **Switch-first, desktop-second.** All renderer work is behind
  `#ifdef __SWITCH__` / `AZAHAR_ENABLE_DEKO3D`.

---

## 3. Phased Plan

### Phase 0 — Foundation & Instrumentation
*Goal: stabilise the build and remove debugging overhead that hides real perf.*

1. **Build hygiene**
   - Pin the devkitPro/Deko3D/uam versions in CI or a build script.
   - Make `AZAHAR_DEKO3D_VALIDATION` and `AZAHAR_DEKO3D_VERBOSE_TELEMETRY`
     opt-in only in Release builds.
   - Add a Release-with-asserts configuration for daily testing.

2. **Telemetry cleanup**
   - Move per-frame verbose logs behind a runtime flag so they do not spam
     logs/serial on every swap.
   - Keep the existing one-second aggregate perf log; it is sufficient.

3. **Regression harness**
   - Add a minimal Deko3D unit test that renders a single colored triangle and
     verifies `deko3d_stats.hw_draw_successes > 0`.
   - Add a soft timeout self-test that fails if the first frame is not presented
     within N seconds.

4. **File structure**
   - Ensure `sdmc:/switch/azahar/` tree is created lazily and missing external
     files produce user-facing warnings instead of fatal exits.

### Phase 1 — CPU / JIT Performance
*Goal: make the ARM11 CPU emulation fast and robust enough that games are not
CPU-bound before GPU work begins.*

1. **Enable safe Dynarmic optimizations on Switch**
   - Default `SwitchDynarmicOptimizations` (currently opt-in via
     `AZAHAR_SWITCH_DYNARMIC_SAFE_OPTIMIZATIONS`).
   - If crashes appear, bisect the offending flag and blacklist only that flag,
     not the whole set.

2. **Reduce JIT publication overhead**
   - Enable `AZAHAR_SWITCH_JIT_PARTIAL_PUBLISH` by default so block emission
     uses `armDCacheFlush` + `armICacheInvalidate` instead of transitioning the
     whole JIT region to executable.
   - Measure `jit_publish_full` vs `jit_publish_partial` counters; aim for
     mostly partial publishes.

3. **Investigate SVC dispatch**
   - Profile whether deferring `CallSVC` to the dispatcher (current Switch
     behaviour) is a measurable overhead.
   - If so, add a safe inline fast-path for common SVCs and fall back to the
     dispatcher only for slow/unknown ones.

4. **Fast memory path**
   - Evaluate Dynarmic inline page-table memory access on Switch. Since
     `fastmem_pointer` is disabled, all guest memory accesses go through
     callbacks. Enabling fastmem with proper unmapped handling could be a
     large win.
   - Do this only after JIT stability is proven, because fastmem changes
     crash-recovery paths.

5. **Deliverable**
   - A build where simple 2D titles run at full speed using only the existing
     (limited) hardware path.

### Phase 2 — Native Audio Output
*Goal: connect the emulator's HLE audio mixer to the Switch speakers/headphones.*

1. **Implement `AudioCore::AudrenSink`**
   - New files `src/audio_core/audren_sink.cpp/h` behind `#ifdef __SWITCH__`.
   - Implement `Sink` interface: `GetNativeSampleRate() == 48000`,
     `SetCallback(...)`, and an internal `audren` voice that pulls samples via
     the callback.
   - Register the sink in `sink_details.cpp` when `__SWITCH__` is defined.

2. **Change default Switch audio settings**
   - In `switch_paths.cpp::ApplySwitchSettings`, set
     `output_type = AudioCore::SinkType::Audren` instead of `Null`.

3. **Latency tuning**
   - Start with a 4-6 buffer `audren` voice configuration and measure
     audio/video drift.
   - Add a small ring buffer if `audren` callback timing does not match the
     emulator's sample production cadence.

4. **Deliverable**
   - Boot a game and hear intro audio/video in sync.

### Phase 3 — Deko3D Renderer Core
*Goal: get the majority of commercial games rendering correctly on the GPU.*

This is the largest phase and is broken into sub-streams.

#### 3a. Shader Translation
1. **PICA vertex shader → DKSH**
   - Re-use the existing GLSL shader generator from the OpenGL renderer
     (`video_core/renderer_opengl/gl_shader_manager.cpp`) but adapt it to
     emit GLSL compatible with `uam` and the Deko3D vertex attribute layout.
   - Replace the fixed `pica_color_vsh.glsl` with generated shaders.
   - Store generated DKSH blobs in `sdmc:/switch/azahar/userdata/shaders/`.

2. **PICA fragment shader (TEV) → DKSH**
   - Port the TEV interpreter/compiler to GLSL. The Vulkan renderer already has
     a SPIR-V generator that is conceptually similar; use it as a reference
     for the TEV stages.
   - Handle up to 4 TEV stages, combiners, and constant colors.

3. **Shader cache / disk cache**
   - Extend `deko3d_shader.cpp` to maintain a `std::unordered_map` keyed by
     PICA shader configuration hash.
   - On cache miss, compile with `uam` and save the DKSH to disk.
   - On startup, preload disk-cached shaders asynchronously.

#### 3b. Texture Pipeline
1. **Implement `TextureCache`**
   - Map PICA texture configurations (address, format, size, wrap, filter) to
     `DkImage`/`DkSampler` objects.
   - Decode tiled/PICA-specific formats on the CPU into a GPU upload buffer
     (RGBA8, DXT, etc. as supported by Deko3D).
   - Use a small LRU eviction policy; Switch RAM is limited.

2. **Texture descriptors**
   - Allocate a descriptor set for textures and samplers per the
   `deko_examples` pattern.
   - Bind descriptor sets once per draw batch and index via
     `dkMakeTextureHandle`.

3. **Remove the `TexturesEnabled` hardware blocker**
   - Once textures are wired, allow textured draws through the hardware path.

#### 3c. Raster State
1. **Depth/stencil**
   - Fix `GetOrCreateDepthTarget` for `D24S8` and make depth compare/write
     states map fully.
   - Remove `DepthTestEnabled`, `DepthWriteEnabled`, and `StencilEnabled`
     hardware blockers.

2. **Alpha test**
   - Implement alpha test in the fragment shader via `discard` (or Deko3D
     alpha-to-coverage where applicable).
   - Remove `AlphaTestUnsupported` blocker.

3. **Blend/logic ops**
   - Map remaining PICA blend factors and equations.
   - For logic ops not directly supported by Deko3D color blend, implement a
     shader-based fallback only when needed, not for every draw.
   - Remove `BlendingEnabled` and `LogicOpUnsupported` blockers.

4. **Viewport/scissor**
   - Map PICA viewport and scissor registers to Deko3D viewport/scissor.
   - Remove `ViewportUnsupported` and `ScissorUnsupported` blockers.

5. **Render target formats**
   - Add format conversion for RGB565 and other formats used by 3DS games.
   - Remove `FramebufferFormat` blocker.

#### 3d. Draw Batch Acceleration
1. **Direct indexed/non-indexed draws**
   - Implement `Rasterizer::AccelerateDrawBatch` for real.
   - Support `TriangleList`, `TriangleStrip`, and `TriangleFan` topologies.
   - Add index buffer upload path.
   - Remove `DirectUnimplemented` / `DirectTopology` blockers.

2. **Geometry shader / procedural textures**
   - Evaluate whether to implement via Deko3D geometry shader or fall back to
     CPU/pre-processing for the first pass.
   - Keep `ShadowRendering` and `ProceduralTexture` blockers until this is
     addressed.

#### 3e. Presentation / Render Target Cache
1. **GPU-side presentation**
   - When a cached render target exists and is GPU-dirty, blit it to the
     appropriate screen region instead of reading back guest VRAM.
   - Fall back to CPU readback only for frames genuinely produced by the CPU.

2. **Render target lifecycle**
   - Improve `State::CachedRenderTarget` ownership tracking so GPU/CPU/Software
     dirty bits drive upload/readback correctly.
   - Add a simple render target cache with size caps.

3. **Display transfers / texture copies**
   - Accelerate common PICA transfer paths with Deko3D blit/scissor/clear
     operations.

### Phase 4 — Input & UX Polish
*Goal: make the port feel like a real Switch app.*

1. **Analog input**
   - Extend `NativeInputState` with `s32 left_stick_x/y`, `right_stick_x/y` and
     pass them through the existing input abstraction to the 3DS circle pad /
     C-stick.

2. **Touch screen**
   - Read `hidGetTouchScreenState` and inject touch events into the 3DS
     touchscreen.

3. **Motion controls**
   - Optional: map `hidSixAxisSensor` to 3DS gyro/accelerometer if a game
     requires it.

4. **Frontend UX**
   - In-emulation menu (pause, save state, load state, settings).
   - Suspend/resume integration (`appletMainLoop`).

### Phase 5 — Performance Hardening
*Goal: hit stable FPS in demanding titles.*

1. **Asynchronous shader compilation**
   - Already enabled in settings; ensure it does not block the first frames.

2. **Command buffer ring / frame pacing**
   - Replace per-draw `dkQueueWaitIdle` and synchronous fence polling with a
     proper multi-frame ring buffer and `DkFence` wait with bounded timeouts.

3. **GPU memory budget**
   - Add a hard cap on render target cache and texture cache sizes to avoid
     OOM on the Switch.

4. **Resolution scaling**
   - Keep native 1x scaling as default; add optional 2x only when frame budget
     allows.

5. **Profile-guided tuning**
   - Use the `time_gpu`, `time_swap`, `time_hle_*` buckets to find which
     subsystem is slow per game.

### Phase 6 — Compatibility & Release
*Goal: broad game compatibility and a user-ready release.*

1. **Game-specific HLE fixes**
   - Replace remaining service stubs with real implementations where needed.
   - Add a lightweight compatibility list / quirks database.

2. **Save states**
   - Complete frontend UI for save/load states and ensure Deko3D GPU state
     restoration works.

3. **Testing matrix**
   - 2D titles → 3D titles → heavy shader titles.
   - Handheld vs docked, different controller configs.

4. **Documentation**
   - Update README with final SD layout, controls, audio requirements, and
     known issues.

---

## 4. Suggested Order of Attack

For the fastest path to "games are playable", tackle the work in this order:

1. **Phase 0** (build/telemetry) — must be done first; it makes everything
   else measurable.
2. **Phase 2** (audio) — small, isolated, big user-visible win.
3. **Phase 1** (JIT) — unlocks CPU headroom before GPU work starts.
4. **Phase 3** (Deko3D core) — the big chunk; do 3a+3b+3c first to get
   textured/depth/blend draws on GPU.
5. **Phase 4** (input/UX) and **Phase 5** (perf) in parallel.
6. **Phase 6** (release polish).

---

## 5. Key Files to Touch

- `src/switch/switch_libnx.cpp` — input/analog/touch, audren lifecycle.
- `src/switch/switch_input.cpp` — input mapping.
- `src/switch/switch_audio.cpp` — sink integration.
- `src/audio_core/audren_sink.cpp` *(new)* — `AudioCore::Sink` implementation.
- `src/audio_core/sink_details.cpp` — register audren sink.
- `src/switch/switch_paths.cpp` — default settings.
- `src/core/arm/dynarmic/arm_dynarmic.cpp` — JIT config.
- `src/video_core/renderer_deko3d/deko3d_rasterizer.cpp/h` — hardware draw path.
- `src/video_core/renderer_deko3d/deko3d_shader.cpp/h` — shader cache & generation.
- `src/video_core/renderer_deko3d/deko3d_texture_cache.cpp/h` — texture cache.
- `src/video_core/renderer_deko3d/deko3d_state.cpp/h` — device/render-target/present.
- `src/video_core/renderer_deko3d/deko3d_presenter.cpp` — final presentation.
- `video_core/renderer_opengl/gl_shader_manager.cpp` and
  `video_core/renderer_vulkan/vk_*.cpp` — reference implementations for shader
  and texture translation.

---

## 6. Risks & Mitigations

| Risk | Mitigation |
|------|------------|
| Deko3D feature gaps vs PICA | Use software rasterizer fallback while features are added; do not remove it until parity is reached. |
| Shader generation with `uam` is slow | Add disk cache and async compile. |
| Switch JIT crashes with optimizations | Bisect per-flag and keep a conservative default set. |
| GPU memory pressure | Cap caches and aggressively evict. |
| Service stubs break games | Implement real services incrementally and maintain a per-game quirks list. |

---

## 7. Definition of Done

A game is considered playable when:
- It boots without fatal errors.
- It renders at native 3DS resolution correctly on the Switch screen.
- It maintains full or near-full speed in normal gameplay.
- Audio is present and in sync.
- Input is responsive and maps cleanly to Switch controls.
- Save states work.
