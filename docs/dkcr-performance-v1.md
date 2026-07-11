# DKCR performance validation — v1

This document validates `feat/gpt-performance-improvements-v1` against the stable full-hardware-rendering `main` baseline. It is focused on measurable performance and regression safety rather than visual spot checks alone.

## Build profiles

Build the performance branch normally:

```bash
./build-switch.sh
```

This profile uses:

- Deko3D validation: off
- verbose graphics telemetry: off
- Switch trace writes: off
- performance diagnostics: off
- partial JIT publication: off
- stable full libnx JIT executable publication: on
- consolidated Dynarmic block publication: on
- CPU-dirty RGBA8 render-target color/depth resolve: on

Dynarmic emits a new block and may then patch older blocks that reference it. Without consolidation, the Switch path publishes once after emission and again after relinking. On libnx `JitType_CodeMemory`, each full publication flushes and invalidates the complete JIT allocation. Consolidated mode keeps the stable full publication path but combines those writes into one publication per emitted block.

A software PICA fallback writes color and possibly depth into Morton-tiled guest memory. That marks the matching Deko3D render target CPU-dirty. The new handoff restores RGBA8 color and active D16, D24, or D24S8 depth into the cached Deko3D images before an otherwise-supported hardware batch proceeds. Unsupported formats and failed transfers remain on the existing software path.

For a diagnostic comparison build:

```bash
SWITCH_PERF_DIAGNOSTICS=ON \
DEKO3D_VERBOSE_TELEMETRY=ON \
JIT_PARTIAL_PUBLISH=OFF \
JIT_CONSOLIDATED_PUBLISH=ON \
./build-switch.sh
```

Partial JIT publication remains an isolated experiment:

```bash
JIT_PARTIAL_PUBLISH=ON ./build-switch.sh
```

Do not use that experimental profile for normal hardware testing. DKCR exited immediately after additional Dynarmic code blocks were created with `partial_publish_enabled=true`, before the first PICA draw or Deko3D presentation activity. Stable builds must report:

```text
partial_publish_enabled=false
```

Consolidated publication can be disabled independently:

```bash
JIT_CONSOLIDATED_PUBLISH=OFF ./build-switch.sh
```

The CPU-dirty handoff can be disabled independently without reverting the branch:

```bash
./build-switch.sh -DAZAHAR_SWITCH_DEKO3D_CPU_DIRTY_RESOLVE=OFF
```

After changing any of these compile-time modes, perform one clean rebuild:

```bash
rm -rf build-switch
./build-switch.sh
```

The normal configuration should print:

```text
Switch Deko3D CPU-dirty render-target resolve enabled
Switch Dynarmic consolidated publication enabled
```

## Current DKCR hardware findings

The first stable test with partial publication disabled reached the title flow without a process exit. Colors, title-screen elements, and composition were reported as correct. No Deko3D queue error or GPU timeout was visible.

Consolidated full JIT publication materially improved startup and title progression:

- first frame: approximately 14.4 s to 8.4 s
- later measured title-flow milestones: approximately 42–46% faster

A subsequent diagnostic sample isolated the next bottleneck:

```text
FPS 1.41 SystemFPS 2.82 Speed 4.72%
GPU 342.40ms Swap 2.87ms
HW draws=0 SW fallback=602
JIT calls=965 blocks=0 partial_pub=0 full_pub=0
```

The matching graphics summary reported 602 valid transformed batches, zero eligible batches, zero submitted hardware batches, and 602 software fallbacks in that interval. The repeated rejection was:

```text
Deko3D HW reject: color RT is CPU-dirty
```

Because that interval contained no newly compiled JIT blocks or JIT publications, further JIT changes would not address this specific stall. The software-to-Deko3D ownership handoff is therefore the current test target.

## CPU-dirty handoff behavior

The handoff is deliberately fail-closed:

1. It runs only after the batch has passed the existing shader, texture, state, and render-target checks.
2. It currently accepts only RGBA8 color targets.
3. It detiles and vertically restores the software-rendered color image from guest memory.
4. When depth testing or depth writing is active, it also restores D16, D24, or D24S8 depth data.
5. It waits for the temporary copy to complete before releasing staging resources.
6. It clears CPU-dirty ownership only after a successful transfer.
7. Any unsupported format, missing pointer, allocation failure, queue error, or fence failure retains the original software fallback.

A successful diagnostic run should contain a rate-limited line like:

```text
Deko3D CPU->GPU render-target resolve color=0x........ depth=0x........ size=240x800 ...
```

The following counters should improve in the previously latched title scene:

- `HW draws` becomes nonzero
- `eligible` and `submitted` become nonzero
- `SW fallback` falls below the previous 602-per-second sample
- `fallback_wrong_render_target` or equivalent CPU-dirty rejection pressure declines
- no raster or present queue errors/timeouts appear

The first implementation uses a synchronous transition and temporary staging allocation for correctness. If hardware validation succeeds, the follow-up optimization is a persistent fence-protected resolve ring to remove allocation and wait overhead.

## Immediate rollback conditions

Disable `AZAHAR_SWITCH_DEKO3D_CPU_DIRTY_RESOLVE` immediately if any of these appear:

- missing or corrupted title-screen elements
- incorrect color channels or vertical orientation
- depth inversion, objects drawing through each other, or stale depth
- Deko3D queue error or fence timeout
- black/white-screen regression
- crash during the first software-to-hardware transition

Rollback build:

```bash
rm -rf build-switch
./build-switch.sh -DAZAHAR_SWITCH_DEKO3D_CPU_DIRTY_RESOLVE=OFF
```

This leaves consolidated JIT publication and the previously validated renderer changes enabled.

## Submodule safety and branch policy

The `azahar-nx/main`, `dynarmic-nx/main`, and `oaknut-nx/main` branches are treated as one tested, compatible baseline. Performance development must not make Azahar builds follow the moving `main` head of either dependency.

For this v1 branch:

- use the exact Dynarmic and Oaknut commits recorded by the Azahar gitlinks
- do not replace the gitlinks at build time by cloning dependency `main`
- do not modify `dynarmic-nx/main` or `oaknut-nx/main` directly
- initialize only the nested Dynarmic dependencies required by the ARM64 build
- do not recursively follow Dynarmic's nested Oaknut gitlink; Azahar supplies its compatible top-level Oaknut checkout

Safe local synchronization:

```bash
git fetch origin
git switch feat/gpt-performance-improvements-v1
git pull --ff-only origin feat/gpt-performance-improvements-v1

git submodule sync
git submodule update --init --jobs 4

git -C externals/dynarmic submodule sync
git -C externals/dynarmic submodule update --init \
  externals/mcl \
  externals/robin-map
```

Do not use unrestricted recursion for this dependency layout:

```bash
git submodule update --init --recursive
```

When Dynarmic or Oaknut source changes are required, create a dedicated branch in each affected fork, pin its exact tested commit in Azahar, merge dependency PRs first, verify the pinned commits are reachable from dependency `main`, and merge the Azahar PR last. No dependency source change was required for the CPU-dirty handoff.

## DKCR test conditions

Use the same console, firmware, Atmosphere version, game region/update, SD card, power mode, clock profile, and scene for comparisons. Start with stock clocks and a cold application launch.

Record at least:

1. Boot and title transition
2. File-select screen
3. World map idle
4. First level opening camera movement
5. First level normal traversal
6. Dense effects or enemy section
7. Pause and resume
8. Level exit or death/reload

For each checkpoint record FPS, emulation speed, audio behavior, frame pacing, visual correctness, stabilization time, queue errors/timeouts, hardware draw count, software fallback count, and CPU-to-GPU resolve count.

Do not compare a shader/texture-cache warm run against a cold baseline run.

## Acceptance target

The v1 branch is ready for the next direct-draw phase only when all of these are true:

- no crash or Deko3D queue error across three complete level runs
- no color, orientation, depth, blending, or cached-render-target regression
- hardware draw coverage increases repeatably in the previously CPU-dirty scene
- emulation speed improves repeatably over the same scene and clock profile
- audio remains synchronized
- no progressive memory degradation during a 20-minute session

A fully playable DKCR target still requires native indexed/non-indexed submission with PICA vertex-shader translation, replacing the CPU-transformed `AddTriangle()` path. The CPU-dirty handoff removes an ownership latch; it does not eliminate that larger architectural cost.
