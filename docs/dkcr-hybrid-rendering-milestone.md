# DKCR hybrid rendering milestone

## Objective

Move from the current correct software-dominant title path to a selective hybrid path:

- the packed `240x800` top-screen render target transitions to Deko3D
- its two `240x400` eye crops become independent destination snapshots
- the `240x320` lower screen transitions through a guest mirror plus Deko3D snapshot
- supported display transfers become point-in-time destination snapshots instead of aliases to mutable source render targets
- software fallback remains active for every unsupported or unsafe target

The validated title-scene acceptance condition is correct output with zero software fallback, while unsupported targets retain the fallback path.

## Restored baseline checkpoint

With CPU-dirty resolve disabled, DKCR's title screen returned completely on both upper and lower displays. The run presented its first frame, initialized DSP, reached CRR/save/CRO startup milestones, and showed no Deko3D queue error or fence timeout in the captured section. This is the visual baseline the hybrid profile must preserve.

## Why snapshots are required

The first global CPU-dirty experiment stored display transfers as aliases to their source render targets. DKCR later changed the source ownership before the next present, which erased the top alias. The resulting run produced thousands of hardware draws but a black upper screen and white lower screen.

A display transfer must preserve the image at transfer time. The new path creates a cached destination image keyed by the guest output address and copies the supported source region into it. Later source invalidation no longer destroys the transferred frame.

## Supported first pass

The snapshot path accepts only:

- GPU-owned source allocation, including a row-aligned address inside a packed target
- RGBA8 source render targets
- RGBA8 or RGB8 display output
- no scaling
- no vertical flip
- no 32x32 block mode
- output dimensions no larger than the source

Unsupported transfers return to the existing software blitter.

The CPU-dirty resolver accepts DKCR's packed `240x800` source. Display-transfer snapshots map its base to rows 0-399 and its 384,000-byte eye offset to rows 400-799.

## Stable build

```bash
rm -rf build-switch
./build-switch.sh
```

Expected configuration:

```text
Switch Deko3D display-transfer snapshots enabled
Switch Deko3D CPU-dirty render-target resolve enabled
Switch Dynarmic consolidated publication enabled
```

This profile must retain the restored title screen on both screens.

## Hybrid diagnostic build

```bash
rm -rf build-switch

SWITCH_PERF_DIAGNOSTICS=ON \
DEKO3D_VERBOSE_TELEMETRY=ON \
DEKO3D_DISPLAY_TRANSFER_SNAPSHOTS=ON \
DEKO3D_CPU_DIRTY_RESOLVE=ON \
JIT_PARTIAL_PUBLISH=OFF \
JIT_CONSOLIDATED_PUBLISH=ON \
./build-switch.sh
```

Expected log markers include:

```text
Deko3D display-transfer snapshot ... out=240x400 source_y=400 ...
Deko3D display-transfer snapshot ... out=240x320 source_y=0 ...
```

The desired title-scene counters are:

```text
HW draws > 0
SW fallback = 0
raster_qerr = 0
raster_to = 0
present_qerr = 0
present_to = 0
```

## Visual acceptance

- upper title screen remains fully visible
- lower screen is not white, stale, rotated incorrectly, or missing
- colors and alpha composition match the stable profile
- no progressive corruption while the framebuffer addresses alternate
- file-select and world-map transitions remain correct

## Immediate rollback

```bash
rm -rf build-switch
DEKO3D_CPU_DIRTY_RESOLVE=OFF ./build-switch.sh
```

The snapshot path can also be disabled independently:

```bash
rm -rf build-switch
DEKO3D_DISPLAY_TRANSFER_SNAPSHOTS=OFF \
DEKO3D_CPU_DIRTY_RESOLVE=OFF \
./build-switch.sh
```

## Follow-up after validation

The packed top target, both eye crops, alternating destination addresses, and lower snapshot path are hardware validated. The next optimization is replacing temporary synchronous resolve/snapshot allocations with small fence-protected transfer rings.
