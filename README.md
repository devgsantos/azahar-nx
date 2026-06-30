# Azahar NX

Azahar NX is a native Nintendo Switch fork of [Azahar](https://github.com/azahar-emu/azahar), an open-source Nintendo 3DS emulator based on Citra.

This fork keeps the Azahar/Citra core architecture and adds a Switch-native frontend, libnx platform layer, Joy-Con input path, audren audio setup, Switch filesystem layout, and an early Deko3D renderer path.

The current goal is a first playable Switch build.

## Current Status

Working or implemented:

- Native Switch executable target: `azahar_switch`
- ROM browser for `.3ds`, `.cci`, `.cxi`, `.app`, and `.cia` entries
- Fixed Switch filesystem root under `sdmc:/switch/azahar/`
- Switch HID input with fixed Joy-Con mapping
- audren audio initialization
- Switch network/service stubs for offline boot paths
- Deko3D renderer selection and initialization path
- Early boot and renderer tracing under `sdmc:/switch/azahar/logs/`

Still in progress:

- Full PICA-to-Deko3D rasterizer implementation
- Complete texture/shader translation for gameplay rendering
- Performance tuning
- Save-state and advanced frontend workflows
- Broad game compatibility validation

## SD Card Layout

Create this directory tree on the SD card:

```text
sdmc:/switch/azahar/
sdmc:/switch/azahar/roms/
sdmc:/switch/azahar/config/
sdmc:/switch/azahar/cache/
sdmc:/switch/azahar/logs/
sdmc:/switch/azahar/userdata/
sdmc:/switch/azahar/userdata/keys/
sdmc:/switch/azahar/userdata/sysdata/
sdmc:/switch/azahar/userdata/nand/
sdmc:/switch/azahar/userdata/sdmc/
sdmc:/switch/azahar/userdata/shaders/
```

Place ROMs in:

```text
sdmc:/switch/azahar/roms/
```

Required external data currently checked at startup:

```text
sdmc:/switch/azahar/userdata/keys/aes_keys.txt
sdmc:/switch/azahar/userdata/sysdata/seeddb.bin
sdmc:/switch/azahar/userdata/sysdata/shared_font.bin
```

## Running

Build or copy the NRO to:

```text
sdmc:/switch/azahar/azahar.nro
```

Launch it from your preferred homebrew loader.

Controls in the current frontend:

```text
D-Pad / Left Stick  Move ROM selection
A                   Boot selected ROM
B                   Exit browser or dismiss fatal error
Plus + Minus        Request shutdown during emulation
```

## Logs

Main log:

```text
sdmc:/switch/azahar/logs/azahar.log
```

Early boot trace:

```text
sdmc:/switch/azahar/logs/azahar-switch-early.log
```

The early trace is the most useful file when debugging startup or renderer failures. It records ordered `TRACE` lines for platform setup, core load, service initialization, GPU creation, Deko3D device/queue/framebuffer creation, and first-frame presentation.

## Build Requirements

The Switch target is built with devkitPro/devkitA64 and libnx.

Expected environment:

```text
DEVKITPRO=/opt/devkitpro
DEVKITA64=/opt/devkitpro/devkitA64
```

Configure:

```bash
cmake -S . -B build-switch \
  -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DENABLE_SWITCH_FRONTEND=ON \
  -DENABLE_QT=OFF \
  -DENABLE_OPENGL=OFF \
  -DENABLE_VULKAN=OFF \
  -DENABLE_WEB_SERVICE=OFF \
  -DENABLE_SCRIPTING=OFF \
  -DENABLE_GDBSTUB=OFF \
  -DENABLE_TESTS=OFF \
  -DCITRA_USE_PRECOMPILED_HEADERS=OFF \
  -DENABLE_LTO=OFF
```

Build:

```bash
cmake -S . -B build-switch
cmake --build build-switch --target azahar_switch -- -j2
```

Live debugging with nxlink:

```bash
/opt/devkitpro/tools/bin/nxlink \
  -a SWITCH_IP \
  -s build-switch/bin/Release/azahar.nro
```

Output:

```text
build-switch/bin/Release/azahar.nro
build-switch/bin/Release/azahar_switch.elf
```

## Development Notes

Switch-specific code lives primarily in:

```text
src/switch/
src/video_core/renderer_deko3d/
src/common/switch_trace.*
src/network/switch_network_stub.cpp
src/core/hle/service/*_switch_stub.cpp
```

The Switch build must use Deko3D. Alternate renderer or window bootstrap paths are not valid runtime fallbacks for this target.

If Deko3D initialization fails, the frontend should stay open, show a clean error, and return to the ROM browser instead of closing.

## Thanks

Special thanks to the original Azahar team for the current emulator project and to the Citra developers whose work forms the foundation of the emulator core.

This fork also builds on the work from the Lime3DS project and PabloMK7's Citra fork, which were merged to create Azahar.

## License

Azahar NX follows the same licensing terms as upstream Azahar.

The project is licensed under GPLv2 or any later version. See [license.txt](license.txt) for the full license text.

All original copyright notices, source headers, third-party notices, and external dependency licenses remain in effect.

## Disclaimer

This is an unofficial fork focused on Switch homebrew development. It is not an official Azahar release.

Use your own legally obtained system files and game dumps. This repository does not provide games, keys, firmware, BIOS files, or copyrighted system data.
