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
- partial JIT publication: off
- stable full libnx JIT executable transition: on

For a diagnostic comparison build:

```bash
SWITCH_PERF_DIAGNOSTICS=ON \
DEKO3D_VERBOSE_TELEMETRY=ON \
./build-switch.sh
```

Partial JIT publication is retained only as an experimental, isolated comparison:

```bash
JIT_PARTIAL_PUBLISH=ON ./build-switch.sh
```

Do not use that experimental profile for normal hardware testing. DKCR exited immediately after additional Dynarmic code blocks were created with `partial_publish_enabled=true`, before the first PICA draw or Deko3D presentation activity. The stable profile therefore keeps `jitTransitionToExecutable()` in the publication path.

After pulling the rollback commit, perform one clean rebuild so the previous compile definition cannot remain in cached objects:

```bash
rm -rf build-switch
./build-switch.sh
```

The startup log must report:

```text
partial_publish_enabled=false
```

## Submodule safety and branch policy

The `azahar-nx/main`, `dynarmic-nx/main`, and `oaknut-nx/main` branches are treated as one tested, compatible baseline. Performance development must not make Azahar builds follow the moving `main` head of either dependency.

For this v1 branch:

- use the exact Dynarmic and Oaknut commits recorded by the Azahar gitlinks
- do not replace the gitlinks at build time by cloning dependency `main`
- do not modify `dynarmic-nx/main` or `oaknut-nx/main` directly
- initialize only the nested Dynarmic dependencies actually required by the ARM64 build
- do not recursively follow Dynarmic's nested Oaknut gitlink; Azahar already supplies its compatible top-level Oaknut checkout

Safe local synchronization for the current branch:

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

Do not use an unrestricted recursive update for this dependency layout:

```bash
git submodule update --init --recursive
```

### When Dynarmic or Oaknut changes are required

Create a dedicated branch in each dependency that actually needs modification. Prefer a matching integration name, for example:

- `devgsantos/dynarmic-nx:feat/gpt-performance-improvements-v1`
- `devgsantos/oaknut-nx:feat/gpt-performance-improvements-v1`

Then follow this sequence:

1. Branch from the dependency's tested `main` head.
2. Apply and validate the dependency-only changes on that feature branch.
3. Update `azahar-nx:feat/gpt-performance-improvements-v1` to pin the exact dependency commit SHA in its gitlink.
4. Build Azahar from those exact pinned commits; never resolve a floating branch head inside CI.
5. Keep dependency branches available and avoid force-pushing them while Azahar validation is active.
6. Merge validated dependency pull requests first.
7. Confirm the pinned commits are now reachable from each dependency's `main`.
8. Merge the Azahar pull request last.

This merge order ensures that `azahar-nx/main` never points to a dependency commit that is available only on a temporary branch. If no dependency source change is required, retain the existing Azahar gitlinks and do not create unnecessary dependency branches.

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

### An early process exit returns

First verify that every JIT allocation reports `partial_publish_enabled=false`. If it reports `true`, the wrong or stale build is running.

If it still exits before the first PICA draw with partial publication disabled, use a diagnostic build to restore Dynarmic breadcrumbs and capture the Switch exception dump before changing renderer synchronization.

### A GPU timeout or renderer crash returns

Test these rollback points independently:

1. diagnostic build with Deko3D validation enabled
2. reduce the asynchronous draw ring to the previous three entries
3. disable asynchronous texture upload while retaining tile-based decode

Do not merge until the exact regression source is isolated.

## Acceptance target for the next phase

The v1 branch is ready for the direct-draw phase when all of the following are true:

- no crash or Deko3D queue error across three complete level runs
- no white-screen or cached-render-target regression
- audio remains synchronized
- repeatable improvement over `main` in the same scene and clock profile
- no progressive memory degradation during a 20-minute session

A fully playable DKCR target still requires profiling-driven work after v1. The largest known architectural gap is native indexed/non-indexed submission with PICA vertex-shader translation, replacing the CPU-transformed `AddTriangle()` path.
