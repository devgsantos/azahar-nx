# Azahar NX — Performance Improvement Plan (CPU-dirty / HW-rejection focus)

This is a focused addendum to [`switch-implementation-plan.md`](./switch-implementation-plan.md). It concentrates on the current stable bottleneck: **hardware draws are rejected because render targets are marked CPU-dirty**, which pushes work to the slow software rasterizer.

The goal is to **measure first, then reduce the CPU-dirty rate** without adding heavy instrumentation that itself distorts frame time.

---

## 1. Diagnosis: where CPU-dirty comes from

A render target becomes `cpu_dirty` inside `State::InvalidateRenderTargetsOverlapping` (`src/video_core/renderer_deko3d/deko3d_state.cpp:1087`). That function is called for three reasons:

| Caller | Owner | Why it happens |
|--------|-------|----------------|
| `Rasterizer::FlushRegion` / `InvalidateRegion` / `FlushAndInvalidateRegion` | `CpuMemory` | Guest CPU writes to a page that overlaps a cached GPU render target. |
| `Rasterizer::DrawSoftwareFallback` | `SoftwareRasterizer` | The software fallback just wrote the same memory, so the next draw must not read stale GPU data. |
| Display-transfer / memory-fill write paths | `DisplayTransfer` | The PICA transfer engine wrote into a render-target region from the GPU side. |

The per-second log already prints `fallback_wrong_render_target`, which covers the rejection. The first step is to know **which owner triggered the dirty marking**, so we do not waste effort fixing the wrong source.

### New lightweight counters (added by this plan)

Three counters split `render_target_cpu_dirty` by owner:

* `render_target_cpu_dirty_by_cpu_memory`
* `render_target_cpu_dirty_by_software`
* `render_target_cpu_dirty_by_display_transfer`

They are atomic `fetch_add` calls on the exact same path that already increments `render_target_cpu_dirty`, so the runtime cost is negligible and cannot create a false-negative performance regression.

---

## 2. High-level improvement plan

### 2.1 CPU-dirty mitigation (highest impact)

1. **Quantify** — enable the counters above and run a representative title. If `by_cpu_memory` dominates, the issue is the CPU writing into VRAM/linear-heap pages that happen to alias render targets. If `by_software` dominates, the software fallback itself is creating a self-reinforcing loop.
2. **Coarse page tracking** — `RasterizerCacheMarker` already marks pages as `RasterizerCachedMemory`. On a CPU write we call `RasterizerFlushVirtualRegion` with the exact virtual range. That is correct but pessimistic: a single CPU write to a small struct inside a render-target page dirties the whole cached surface. Evaluate a sub-page rectangle tracker (start/size inside the page) for the render-target address ranges.
3. **Batch invalidations** — CPU writes often come in bursts (memcpy, DMA, clear). Instead of invalidating on every `MemoryWrite*`, queue the dirty range and flush/merge it at draw submission or at the end of the CPU run slice. This is especially helpful when the CPU writes many bytes to the same render target between GPU draws.
4. **Allow GPU-side CPU-dirty recovery** — when a color target is CPU-dirty but the draw is otherwise supported, consider uploading the stale CPU data to the GPU render target and clearing the dirty flag, rather than falling back to software rasterization. This costs one GPU upload but saves the entire software draw. Only do this when the CPU actually owns the surface and the upload size is small relative to the draw workload.
5. **Separate depth/stencil ownership** — the current code rejects the whole draw if the color target is CPU-dirty, even when only depth changed. Add per-plane dirty tracking so a CPU write to depth does not block color rendering.

### 2.2 JIT performance (second tier)

Current Switch build settings (from `build-switch/CMakeCache.txt`):

* `AZAHAR_SWITCH_DYNARMIC_SAFE_OPTIMIZATIONS=ON` — good.
* `AZAHAR_SWITCH_JIT_PARTIAL_PUBLISH=OFF` — every block emission does a full `jitTransitionToExecutable/Writable` pair. This is expensive.
* `AZAHAR_SWITCH_PERF_DIAGNOSTICS=OFF` — fine for release, but useful for profiling builds.

Recommended actions:

1. **Enable partial publish by default** (`AZAHAR_SWITCH_JIT_PARTIAL_PUBLISH=ON`) and monitor the `partial_publishes` / `full_publishes` counters from `Azahar::Switch::TakeJitPublishStats`. If full publishes are still frequent, tune the Dynarmic block-emission window.
2. **Bisect the safe-optimization set** if any instability appears. The current set explicitly disables `ReturnStackBuffer`. Profile each flag to see if the crash is reproducible and isolate a smaller blacklist.
3. **Defer SVC fast-path only after measuring** — the Switch path currently defers every SVC to the dispatcher. Do not add inline SVC handling until the JIT run stats show it is a measurable portion of the host time.
4. **Fastmem evaluation** — `fastmem_pointer` is disabled on Switch. Once JIT is stable, experiment with a reserved fastmem region and the existing unmapped-page fallback. This can remove one callback per guest memory access.

### 2.3 PICA / command-list performance

1. **Verify the AArch64 PICA shader JIT is active** — `AZAHAR_SWITCH_PICA_SHADER_JIT=ON` in the cache, and `shader_jit.cpp` compiles on `CITRA_ARCH(arm64)`. Confirm at runtime that `fallback_active` stays false; if it trips, the interpreter path is used for all vertex shading.
2. **Avoid redundant `DrawTriangles` flushes** — in `PicaCore::DrawArrays`, the hardware path calls `rasterizer->AccelerateDrawBatch` and returns. The software path calls `LoadVertices` then `DrawTriangles`. Ensure that state-signature hashing and vertex upload are not repeated for tiny indexed batches.
3. **Command-list batching** — `ProcessCmdList` processes one command at a time. Profile whether batching register writes until a draw trigger reduces overhead. This is lower priority than CPU-dirty and JIT.

### 2.4 Deko3D draw-path performance

1. **Remove synchronous waits from the hot path** — `TryDrawHardwareBatch` flushes the queue and waits on a fence per slice. Replace with a ring of in-flight slices and bounded `dkFenceWait` timeouts.
2. **Avoid `std::memcpy` of the whole vertex batch** — upload vertices directly into the CPU-mapped Deko3D memory when possible.
3. **Cache pipeline objects** — blend states are already signature-tracked; extend this to full rasterizer/depth/stencil pipeline objects to reduce Deko3D state validation.

---

## 3. Instrumentation rules (important)

Heavy logging or synchronous SD writes can themselves lower FPS and make a stable build look slow. Follow these rules:

* **Keep the per-second aggregate log**; it is the primary signal.
* **Never add per-draw, per-vertex, or per-memory-write file I/O.** All new diagnostics must be in-memory counters or compile-time-disabled logs.
* **Prefer counters over logs.** Counters are read once per second and cost one atomic increment on the hot path.
* **Use `AZAHAR_SWITCH_PERF_DIAGNOSTICS` for detailed timing**, but only in profiling builds, not in the build users run.
* **When a counter regresses, compare ratios, not absolute numbers.** If `hw_draw_attempts` goes up but `hw_draw_successes` stays flat, the bottleneck shifted, not improved.

---

## 4. Suggested order of work

1. Merge the per-owner CPU-dirty counters and collect one run of representative titles.
2. Address the dominant CPU-dirty owner:
   * `CpuMemory` → coarse tracking / batched invalidation / GPU upload recovery.
   * `SoftwareRasterizer` → reduce self-reinforcing fallback by fixing the next most common rejection reason (likely textures, depth, or blend).
   * `DisplayTransfer` → accelerate display transfers on the GPU so they do not need to invalidate render targets.
3. Enable `AZAHAR_SWITCH_JIT_PARTIAL_PUBLISH` by default and measure JIT publish counters.
4. Tackle remaining Deko3D feature gaps in the order that removes the highest rejection reason counters.

---

## 5. Files to watch

* `src/video_core/renderer_deko3d/deko3d_state.cpp` — render target ownership and dirty logic.
* `src/video_core/renderer_deko3d/deko3d_rasterizer.cpp` — HW/SW draw decision and batch submission.
* `src/video_core/renderer_deko3d/deko3d_stats.h/cpp` — counters and aggregation.
* `src/core/memory.cpp` — `RasterizerFlushVirtualRegion` and `RasterizerCachedMemory` page tracking.
* `src/core/arm/dynarmic/arm_dynarmic.cpp` — JIT config and optimizations.
* `src/switch/switch_jit.cpp` / `switch_dynarmic_jit_bridge.cpp` — JIT publish and run statistics.
* `src/video_core/pica/pica_core.cpp` — command list and draw dispatch.
* `src/video_core/shader/shader_jit.cpp` — PICA vertex shader JIT fallback detection.

---

## 6. Exit criteria

* `render_target_cpu_dirty_by_*` counters show which source is dominant and that source is reduced by at least 30% compared to baseline.
* `hw_coverage_percent` in `TakePerfStats` increases measurably for representative titles.
* JIT `full_publishes` is a small fraction of `partial_publishes + full_publishes`.
* No new heavy logging or per-draw instrumentation is enabled in the default build.
