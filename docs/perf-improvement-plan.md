# Performance Improvement Plan — PICA / JIT / CPU-Dirty

*Status: living document. Update after each measured change.*

---

## Root Cause Summary

The dominant bottleneck is **CPU-dirty render target rejection**: nearly every HW
draw attempt fails because the `CachedRenderTarget::cpu_dirty` flag is set,
forcing the software rasterizer to handle every triangle.

### How `cpu_dirty` gets set (call chain)

```
DrawSoftwareFallback()
  └─ state.MarkRenderTargetSoftwareDirty(fb_address, bytes)
       └─ InvalidateRenderTargetsOverlapping(addr, bytes, SoftwareRasterizer)
            └─ target->cpu_dirty = true   ← blocks next HW draw attempt
```

Once one draw falls back to software (for *any* reason), it marks the RT
cpu-dirty, which forces **all subsequent draws** to the same RT through software
too, creating a cascading failure. The RT only becomes clean again when
`MarkRenderTargetGpuDirty()` is called — which only happens after a *successful*
HW draw.

### Secondary causes of initial SW fallback (from `EvaluateTransformedBatchEligibility`)

Listed by approximate frequency of occurrence in commercial 3DS titles:

| Blocker | Cause | Impact |
|---------|-------|--------|
| `TexturesEnabled` | Texture cache miss, non-Texture2D type, or missing tex shader | Very high |
| `WrongRenderTarget` | RT is `cpu_dirty` (cascade from any prior SW draw) | Very high (derived) |
| `ShadowRendering` | `IsShadowRendering()` true | Medium |
| `FramebufferFormat` | Color format not in {RGBA8, RGB5A1, RGB565, RGBA4} | Low (already wide) |
| `BlendingEnabled` | Unmapped blend factor (rare after recent additions) | Low |
| `DirectUnimplemented` | `AccelerateDrawBatch` always returns false | 100% (all indexed draws) |

---

## Improvement Plan

### Phase A — Break the cpu_dirty Cascade (Highest Priority)

**Goal:** the first successful HW draw must not be blocked by a prior SW draw on the same RT.

#### A1. RT upload path on `cpu_dirty` hit
Instead of rejecting when `cpu_dirty == true`, upload the guest framebuffer
data to the GPU image (similar to how `UploadScreenTextures` works) and clear
the flag. This lets HW draws resume after a software write.

- File: `deko3d_rasterizer.cpp` `TryDrawHardwareBatch()` around line 805
- Add a `state.UploadRenderTargetFromGuest(color_target)` call before rejecting
- Implement `UploadRenderTargetFromGuest` in `deko3d_state.cpp`
- After upload: `color_target->cpu_dirty = false; color_target->gpu_dirty = false;`
- Cost: one CPU→GPU blit per dirty RT per draw batch — acceptable for SW↔HW
  transitions, prevents the cascade

#### A2. Soft cpu_dirty: distinguish SW-rasterizer writes from CPU memory writes
Currently both `SoftwareRasterizer` and `CpuMemory` owner tags set `cpu_dirty`.
SW-rasterizer writes are already captured in the `fallback_vertex_batch` so we
have the pixel data. CPU memory writes (e.g. DMA) are genuinely unknown.

- Add a second flag `software_raster_dirty` (or reuse `owner == SoftwareRasterizer`)
- For `SoftwareRasterizer` owner: attempt RT upload before blocking HW path
- For `CpuMemory` owner: keep blocking (no pixel data available without readback)

#### A3. Prevent unnecessary RT invalidation from software fallback
`DrawSoftwareFallback` currently always calls `MarkRenderTargetSoftwareDirty()`.
This is correct in principle but defeats the HW path for subsequent draws.
After A1 is implemented, the invalidation is still correct; A1 handles recovery.

---

### Phase B — Texture Cache Improvements (Second Priority)

#### B1. Invalidation instead of cache-miss as HW blocker
`GetTexture()` returns `nullptr` on format miss or key not found. Each miss
causes a `LOG_INFO` per draw per second — noisy. Fix:
- Cache "known-unsupported" keys so the reject is instant (no log spam)
- Return a 1×1 fallback pink texture for missing textures instead of rejecting
  — this lets geometry render with wrong colours but keeps the HW path alive

#### B2. Texture cache validation / staleness tracking
Currently the key includes sampler state (wrap, filter) but NOT a
content-generation counter. If a texture is updated in guest memory, the stale
GPU copy is reused. Add a generation counter matching `render_target_generation`
or a lightweight hash of the first N bytes.

#### B3. Synchronous `dkQueueWaitIdle` on every upload
`UploadTexture()` calls `dkQueueWaitIdle` **three times** (drain before,
wait-after-submit, check). This stalls the entire queue including in-flight
raster work. Replace with:
- Submit to a dedicated upload queue (or use `dkQueueWaitIdle` only once)
- Use a fence per upload batch instead of idle-wait

---

### Phase C — JIT Performance (Third Priority)

#### C1. Enable safe Dynarmic optimizations
`config.optimizations = Dynarmic::no_optimizations` is the current default.
`SwitchDynarmicOptimizations` (all_safe & ~RSB) is defined but opt-in only.
Action: flip the default and bisect if a crash occurs.
- File: `arm_dynarmic.cpp` line 611

#### C2. SVC dispatch overhead
Current Switch path: `CallSVC` sets `pending_svc`, halts JIT, returns, then
`HandlePendingSVC()` dispatches. This is two JIT context switches per syscall.
For common SVCs (sleep, signal, wait) this adds measurable overhead.
Consider an inline fast-path table for the 5-10 most common SVCs.

#### C3. Fastmem evaluation
All guest memory accesses go through callbacks (`fastmem_pointer = nullopt`).
Enabling fastmem with a proper guard-page handler could be a 10-30% CPU win.
Gate behind a separate compile flag; requires verified signal handler on Horizon.

#### C4. Per-block flush vs full-region publish
Check whether `AZAHAR_SWITCH_JIT_PARTIAL_PUBLISH` is implemented and enabled.
If not, every block emission triggers a full JIT region transition — expensive.

---

### Phase D — Draw Batch Acceleration (Fourth Priority)

`AccelerateDrawBatch` always returns `false` (`DirectUnimplemented`). All
indexed draws go through the CPU geometry shader path (AddTriangle per vertex).
Implementing real indexed draw support would eliminate the PICA SW vertex
transform for indexed geometry.
- File: `deko3d_rasterizer.cpp` `AccelerateDrawBatch()`
- Target: TriangleList topology first (most common), TriangleStrip/Fan later

---

### Phase E — Presentation / Sync

#### E1. Remove synchronous fence wait inside `SubmitHardwareChunk`
Lines 1254-1258 in `deko3d_rasterizer.cpp` do `dkFenceWait(&slice.fence, -1)`
immediately after submitting — this is a blocking GPU wait inside the draw
path, eliminating the benefit of having frame slices at all.
Replace with: submit + signal fence; wait for the fence only at the *next* use
of that slice (`WaitForFrameSlice` already handles this, but it is bypassed).

#### E2. Separate raster and present queues
`GetRasterQueue()` returns the same queue as the presenter (`queue`). Two queues
on the same device do not fault, but serialisation is coarser. Investigate if a
separate `raster_queue` member (already declared in `State`) can be wired up.

---

## Lightweight Traces Added

The following counters/traces are added to track progress without runtime cost:

| Location | What is tracked | Condition |
|----------|----------------|-----------|
| `TryDrawHardwareBatch` | `cpu_dirty` rejection separate from other RT failures | existing `WrongRenderTarget` path |
| `DrawTriangles` | SW-fallback per second (already present, keep it) | 1-second aggregate |
| `UploadTexture` | Removed 2 of 3 `dkQueueWaitIdle` calls | after B3 |
| `arm_dynarmic.cpp Run()` | JIT ticks_executed / ticks_requested ratio | behind `AZAHAR_SWITCH_PERF_DIAGNOSTICS` |

All new log lines use `LOG_DEBUG` (not `LOG_INFO`) or are aggregated at 1-second
intervals to avoid polluting the log with per-draw noise.

---

## Measurement Baseline

Before each phase: record from the existing 1-second perf log:
- `hw_draw_successes` / `hw_draw_attempts` ratio
- `sw_fallback_draws`
- `render_target_cpu_dirty` counter
- `texture_cache_misses`
- FPS from `game_frame_counter`

Target after Phase A: `render_target_cpu_dirty` drops by >80% and
`hw_draw_successes / hw_draw_attempts` rises from ~0% to >50%.

---

## File Change Index

| File | Phase | Change |
|------|-------|--------|
| `deko3d_rasterizer.cpp` | A1 | Call `UploadRenderTargetFromGuest` on cpu_dirty hit |
| `deko3d_state.cpp/.h` | A1 | Add `UploadRenderTargetFromGuest()` |
| `deko3d_texture_cache.cpp` | B1,B3 | Fallback texture, remove redundant waits |
| `arm_dynarmic.cpp` | C1 | Enable `SwitchDynarmicOptimizations` by default |
| `deko3d_rasterizer.cpp` | E1 | Defer fence wait to next slice use |

---

---

## Implemented (2026-07-12)

| Item | File | Change |
|------|------|--------|
| **C1** — safe JIT opts default-on | `arm_dynarmic.cpp` | `no_optimizations` → `SwitchDynarmicOptimizations` (all_safe & ~RSB); escape hatch: `AZAHAR_SWITCH_DYNARMIC_NO_OPTIMIZATIONS` |
| **E1** — remove blocking fence wait | `deko3d_rasterizer.cpp` | Removed `dkFenceWait(-1)` inside `SubmitHardwareChunk`; fence is now waited in `WaitForFrameSlice` at next slice use |
| **A2 partial** — SW-dirty cascade break | `deko3d_rasterizer.cpp` | `SoftwareRasterizer`-owned cpu-dirty RTs: clear flag + `needs_clear=true` and proceed to HW draw. CPU/DMA-owned: reject with aggregate LOG_DEBUG (1 s) |
| **Log cleanup** | `deko3d_rasterizer.cpp`, `deko3d_texture_cache.cpp` | Per-draw texture miss, RT create fail, depth RT fail → `LOG_DEBUG`. Removed 2 of 3 `dkQueueWaitIdle` calls in `UploadTexture`. Texture upload begin → `LOG_DEBUG` |

### Expected measurable impact
- JIT safe opts: ~15–30% CPU throughput gain (blocked flag was `no_optimizations`)
- SW cascade break: many games will stop bouncing back to SW after the first frame's initial SW draws
- Fence defer: eliminates one GPU stall per HW draw batch
- Log reduction: removes thousands of LOG_INFO per second that themselves cost CPU time

### Next actions (in order)
1. **A1** — implement `UploadRenderTargetFromGuest` for CPU/DMA-dirty RTs (unblocks games that DMA to the framebuffer between frames)
2. **B1** — known-unsupported texture key set to avoid repeated `GetTexture` → null → LOG_DEBUG per draw
3. **D** — `AccelerateDrawBatch` for `TriangleList` indexed draws

*Last updated: 2026-07-12.*
