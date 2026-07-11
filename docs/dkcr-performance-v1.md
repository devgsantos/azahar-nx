# DKCR performance validation — v1

This document validates `feat/gpt-performance-improvements-v1` against the stable full-hardware-rendering `main` baseline. It is focused on measurable performance and regression safety rather than visual spot checks alone.

## Build profiles

Build the stable performance branch normally:

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
- experimental CPU-dirty render-target resolve: off

Dynarmic emits a new block and may then patch older blocks that reference it. Without consolidation, the Switch path publishes once after emission and again after relinking. On libnx `JitType_CodeMemory`, each full publication flushes and invalidates the complete JIT allocation. Consolidated mode keeps the stable full publication path but combines those writes into one publication per emitted block.

For a diagnostic build with counters enabled:

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

The CPU-dirty handoff remains available only for isolated diagnostics:

```bash
DEKO3D_CPU_DIRTY_RESOLVE=ON ./build-switch.sh
```

Do not enable it for normal DKCR testing. The build script explicitly passes the stable `OFF` value so an existing CMake cache cannot silently keep the regressing mode enabled.

After changing JIT publication modes, perform one clean rebuild:

```bash
rm -rf build-switch
./build-switch.sh
```

The normal configuration should print:

```text
Switch Deko3D CPU-dirty render-target resolve disabled (stable)
Switch Dynarmic consolidated publication enabled
```

## Current DKCR hardware findings

The first stable test with partial publication disabled reached the title flow without a process exit. Colors, title-screen elements, and composition were reported as correct. No Deko3D queue error or GPU timeout was visible.

Consolidated full JIT publication materially improved startup and title progression:

- first frame: approximately 14.4 s to 8.4 s
- later measured title-flow milestones: approximately 42–46% faster

A subsequent diagnostic sample isolated a CPU-dirty rejection interval:

```text
FPS 1.41 SystemFPS 2.82 Speed 4.72%
GPU 342.40ms Swap 2.87ms
HW draws=0 SW fallback=602
JIT calls=965 blocks=0 partial_pub=0 full_pub=0
```

The repeated rejection was:

```text
Deko3D HW reject: color RT is CPU-dirty
```

An experimental color/depth resolve was added to test whether this was only an ownership latch. It successfully moved the scene to hardware, but hardware validation disproved that assumption.

## CPU-dirty handoff regression

With the handoff enabled, the diagnostic scene changed to roughly:

```text
HW draws=2640..2960
SW fallback=0
raster queue errors=0
present queue errors=0
```

The screen output regressed at the same time:

- the upper screen became black
- the lower screen became white
- the title screen no longer appeared
- presentation repeatedly selected no top render target
- the bottom presentation continued selecting a cached 240x320 transfer target

Representative presentation state:

```text
SelectPresentRenderTarget: top=0x18000000 top=0x0 addr=0x00000000
bottom=0x18300000 bottom=240x320 addr=0x181d4c00
```

The resolver itself continued reporting successful 240x800 color/depth uploads, and no queue errors or fence timeouts occurred. Therefore this is a correctness failure in ownership/coverage or display-transfer presentation, not a Deko3D device failure.

The important conclusion is that `cpu_dirty` was acting as a correctness barrier. Clearing it globally caused every transformed batch to bypass the software renderer even though the current hardware path and top-screen transfer/presentation chain are not yet equivalent for this scene.

The implementation remains in the branch for controlled research, but stable builds now keep it disabled.

## Requirements before retrying the handoff

Do not re-enable the global handoff until all of the following are implemented or demonstrated:

1. Distinguish a recoverable stale ownership flag from guest memory that still requires software ownership.
2. Preserve or recreate the top display-transfer alias across the complete draw/flush/transfer/present sequence.
3. Verify the 240x800 source-to-240x400 top-screen transform rather than treating it as a plain source alias.
4. Confirm that the fixed transformed hardware shaders reproduce the affected title-screen batches.
5. Permit per-target or per-generation fallback instead of changing all later batches to hardware.
6. Validate color, depth, blending, and both screens before clearing `cpu_dirty` permanently.

A future experiment should start with a single-transition or single-target gate and should automatically return that target to software ownership when its expected display-transfer result is not selected for presentation.

## Stable rollback behavior

Stable builds now force:

```text
AZAHAR_SWITCH_DEKO3D_CPU_DIRTY_RESOLVE=OFF
```

This preserves:

- consolidated full JIT publication
- asynchronous frame-slice reuse
- texture upload/cache improvements
- reduced presentation CPU reads
- the known-correct software fallback after CPU writes

The experimental mode can still be compiled explicitly for instrumentation, but its output is not considered valid.

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

When Dynarmic or Oaknut source changes are required, create a dedicated branch in each affected fork, pin its exact tested commit in Azahar, merge dependency PRs first, verify the pinned commits are reachable from dependency `main`, and merge the Azahar PR last. No dependency source change was required for this rollback.

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

For each checkpoint record FPS, emulation speed, audio behavior, frame pacing, visual correctness, stabilization time, queue errors/timeouts, hardware draw count, software fallback count, and presentation source selection.

Do not compare a shader/texture-cache warm run against a cold baseline run.

## Acceptance target

The v1 branch is ready for further optimization only when all of these are true:

- the title screen is visible on the stable profile
- both 3DS screens present the expected content
- no crash or Deko3D queue error across three complete level runs
- no color, orientation, depth, blending, or cached-render-target regression
- emulation speed improves repeatably over the same scene and clock profile
- audio remains synchronized
- no progressive memory degradation during a 20-minute session

A fully playable DKCR target still requires native indexed/non-indexed submission with PICA vertex-shader translation, replacing the CPU-transformed `AddTriangle()` path. The failed global CPU-dirty handoff must not be used as a substitute for that architectural work.
