# DKCR performance validation — v1

This document validates `feat/gpt-performance-improvements-v1` against the stable full-hardware-rendering `main` baseline. It is intentionally focused on measurable performance and regression safety rather than visual spot checks alone.

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
- partial JIT publication: on

For a diagnostic comparison build:

```bash
SWITCH_PERF_DIAGNOSTICS=ON \
DEKO3D_VERBOSE_TELEMETRY=ON \
./build-switch.sh
```

For an immediate partial-JIT rollback build:

```bash
JIT_PARTIAL_PUBLISH=OFF ./build-switch.sh
```

## Test conditions

Use the same console, firmware, Atmosphere version, game region/update, SD card, power mode, and clock profile for both `main` and this branch.

Recommended first pass:

- official docked or handheld stock clock profile
- no unsafe overclock
- airplane mode when network behavior is irrelevant
- cold application launch before each run
- identical save state or level entry point
- at least three runs per scene

Do not compare a shader/texture-cache warm run against a cold baseline run.

## DKCR scenes

Record at least these checkpoints:

1. Boot and title transition
2. File-select screen
3. World map idle
4. First level opening camera movement
5. First level normal traversal
6. Dense effects or enemy section
7. Pause and resume
8. Level exit or death/reload

For each checkpoint, record:

- displayed FPS
- emulation speed percentage
- audio quality: stable, crackling, delayed, or muted
- frame pacing: smooth, periodic hitch, continuous stutter, or freeze
- visual correctness: top screen, bottom screen, textures, depth, blending, shadows
- time until the scene becomes stable after loading
- crash, GPU timeout, black screen, or white-screen regression

## Interpretation

### FPS rises and speed rises

The removed queue stalls or reduced CPU work were a material bottleneck. Continue with direct draw submission and state caching.

### FPS rises but speed remains low

Rendering is presenting more often, but ARM11 emulation, PICA vertex processing, DSP, or game-thread timing remains dominant. Profile Dynarmic host time and transformed geometry coverage next.

### Speed rises but FPS remains low

The core is advancing faster, but presentation or GPU throughput remains dominant. Inspect fence reuse waits, swap time, fragment load, and render-target resolution.

### Large hitches remain during new scenes

Texture decode/upload or JIT compilation is still visible. Compare cold and warm runs and inspect whether hitches disappear after assets and code blocks are cached.

### Performance improves, then degrades over time

Inspect texture-cache eviction frequency, render-target growth, memory pressure, and repeated invalidation ranges.

### A GPU timeout or crash returns

Test these rollback points independently:

1. `JIT_PARTIAL_PUBLISH=OFF`
2. diagnostic build with Deko3D validation enabled
3. reduce the asynchronous draw ring to the previous three entries
4. disable asynchronous texture upload while retaining tile-based decode

Do not merge until the exact regression source is isolated.

## Acceptance target for the next phase

The v1 branch is ready for the direct-draw phase when all of the following are true:

- no crash or Deko3D queue error across three complete level runs
- no white-screen or cached-render-target regression
- audio remains synchronized
- repeatable improvement over `main` in the same scene and clock profile
- no progressive memory degradation during a 20-minute session

A fully playable DKCR target still requires profiling-driven work after v1. The largest known architectural gap is native indexed/non-indexed submission with PICA vertex-shader translation, replacing the CPU-transformed `AddTriangle()` path.
