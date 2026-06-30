You are working on a native Nintendo Switch port of a Nintendo 3DS emulator based on the Azahar/Citra architecture.

# Project Goal

Create an aggressive first playable Nintendo Switch port.

The objective is NOT to build the complete emulator frontend.

The objective is to reach a point where:

- The application launches as an NRO.
- A minimal native Switch frontend appears.
- The user selects a game.
- The selected game boots.
- Emulation starts.
- Rendering begins.
- Input works.
- Audio works.
- Performance-oriented architecture is already in place.

Do NOT work in tiny debug milestones.

Do NOT stop after every subsystem asking for testing.

Perform one large coherent implementation pass across the platform layer.

Only stop when a meaningful playable milestone has been reached.

If something cannot yet be completed, leave a TODO and continue implementing everything else.

Never fake success.

Architect everything correctly even if one subsystem is temporarily incomplete.

------------------------------------------------------------
PRIMARY SUCCESS CRITERIA
------------------------------------------------------------

The first implementation is considered successful only when:

✓ NRO launches
✓ Minimal frontend appears
✓ ROM browser works
✓ User selects a game
✓ Emulator loads ROM
✓ Emulation loop starts
✓ Frames are presented
✓ Joy-Con input works
✓ Audio initializes
✓ FPS/speed overlay is available
✓ Logs clearly describe the boot process

------------------------------------------------------------
FRONTEND
------------------------------------------------------------

Completely replace the desktop frontend.

Do NOT port Qt.

Do NOT port desktop menus.

Do NOT port desktop dialogs.

Frontend requirements:

- Native Switch UI
- Text UI, ImGui or lightweight custom UI
- Fast startup
- Minimal code
- No unnecessary abstractions

Required functionality only:

• Browse ROMs
• Select ROM
• Launch ROM
• Show loading status
• Show fatal errors
• Optional FPS overlay

No settings menu.

No updater.

No multiplayer UI.

No desktop features.

------------------------------------------------------------
FILESYSTEM POLICY
------------------------------------------------------------

Everything related to this emulator must live inside:

sdmc:/switch/azahar/

Do NOT create folders outside this directory except when explicitly required by Horizon OS.

The emulator must never depend on:

- current working directory
- HOME
- Documents
- Qt StandardPaths
- desktop filesystem assumptions

Every internal path must be relative to:

sdmc:/switch/azahar/

Directory layout:

sdmc:/switch/azahar/

├── azahar.nro
├── config/
├── cache/
├── logs/
├── resources/
├── roms/
└── userdata/
    ├── saves/
    ├── states/
    ├── screenshots/
    ├── cheats/
    ├── shaders/
    ├── nand/
    ├── sdmc/
    ├── system/
    ├── keys/
    └── bios/

Rules:

config/
- emulator configuration

cache/
- temporary cache
- pipeline cache
- compiled caches

logs/
- runtime logs

resources/
- fonts
- icons
- UI assets

roms/
- default ROM search directory

userdata/saves/
- save data

userdata/states/
- save states

userdata/screenshots/
- screenshots

userdata/cheats/
- cheats

userdata/shaders/
- shader cache

userdata/nand/
- virtual NAND

userdata/sdmc/
- virtual 3DS SD card

userdata/system/
- system archives

userdata/keys/
- encryption keys if required

userdata/bios/
- BIOS/system files if required

------------------------------------------------------------
ROM BROWSER
------------------------------------------------------------

Browse:

sdmc:/switch/azahar/roms/

Supported formats:

.3ds
.cci
.cia
.3dsx

Controller:

A -> Launch

B -> Back

DPad / Left Stick -> Navigate

------------------------------------------------------------
REUSE FROM AZAHAR
------------------------------------------------------------

Reuse as much of the emulator core as possible.

Keep:

CPU

Memory

Kernel

Loader

Filesystem logic

Config parser

Game database

Save manager

Shader cache logic

Texture cache logic

HLE services

DSP

Crypto

Network

RTC

Cheats

RomFS

NCCH

NCSD

Exefs

CIA loader

3DSX loader

------------------------------------------------------------
REMOVE
------------------------------------------------------------

Remove or bypass:

Qt

QMainWindow

Qt Widgets

Qt dialogs

Qt filesystem

Qt input

Qt audio

Qt event loop

Desktop OpenGL window

Desktop menus

Desktop updater

Desktop platform layer

------------------------------------------------------------
INPUT
------------------------------------------------------------

Replace desktop backend with Switch HID.

Provide default mapping immediately.

Do NOT block boot waiting for configurable controls.

Map:

A

B

X

Y

L

R

ZL

ZR

Plus

Minus

DPad

Left Stick

Right Stick

HOME handled by Horizon

Touch support optional

------------------------------------------------------------
AUDIO
------------------------------------------------------------

Replace desktop audio.

Use Switch-native backend.

Requirements:

Low latency

Stereo

Non-blocking

If audio initialization fails:

Log error

Continue boot whenever possible

------------------------------------------------------------
RENDERER
------------------------------------------------------------

Desktop renderer must not remain.

Architecture:

Game

↓

PICA200

↓

VideoCore

↓

Renderer abstraction

↓

Deko3D backend

Do NOT tightly couple Deko3D to frontend code.

Keep VideoCore intact.

Only replace the backend implementation.

Performance priorities:

Avoid GPU readbacks.

Avoid CPU texture copies.

Avoid synchronous GPU waits.

Support shader cache.

Support pipeline cache.

Design renderer for future optimization.

------------------------------------------------------------
CPU / JIT
------------------------------------------------------------

Enable ARM64 JIT by default.

Never silently fall back to interpreter because of old config files.

Force:

use_jit=true

use_cpu_jit=true

debug_cpu=false

Provide migration if old configs exist.

Switch JIT requirements:

Executable memory

Proper page permissions

Instruction cache flushing

Large preallocated code cache

Avoid per-block allocations

Remove desktop allocation assumptions

If JIT initialization fails:

Display clear fatal error

Do NOT silently switch to interpreter

------------------------------------------------------------
MEMORY
------------------------------------------------------------

Replace desktop assumptions.

Use aligned allocations.

Preallocate large regions.

Avoid frequent malloc/free.

Avoid allocation in hot paths.

Prepare for full application mode memory.

Do not optimize for applet mode.

------------------------------------------------------------
THREADING
------------------------------------------------------------

Review all thread priorities.

Recommended:

CPU thread

GPU thread

Audio thread

Loader thread

Avoid:

Busy waiting

Filesystem stalls

Shader compilation stalls

Blocking render thread

------------------------------------------------------------
DEFAULT PERFORMANCE PROFILE
------------------------------------------------------------

Performance is more important than graphical enhancements.

Default settings:

Native internal resolution

JIT enabled

Shader cache enabled

Texture cache enabled

Frame pacing enabled

Async shader compilation if available

Disable:

Texture enhancement

Post processing

Resolution scaling

Expensive accuracy options

Verbose validation

Telemetry

Development diagnostics

------------------------------------------------------------
BUILD SYSTEM
------------------------------------------------------------

Create a dedicated Switch target.

Do not break desktop builds.

Release configuration:

Optimized

NDEBUG

No Qt

Switch platform defines

Generate:

NRO

Default config

Assets

README

------------------------------------------------------------
LOGGING
------------------------------------------------------------

Keep logs concise.

Log categories:

BOOT

FILESYSTEM

INPUT

AUDIO

RENDERER

MEMORY

JIT

ROM LOADER

FIRST FRAME

FATAL

Verbose logging only under:

DEBUG_VERBOSE

Disabled by default.

------------------------------------------------------------
ERROR HANDLING
------------------------------------------------------------

Show fatal messages on screen.

Examples:

ROM load failed

Unsupported format

Missing system files

Renderer initialization failed

JIT initialization failed

Memory initialization failed

Audio initialization failed

------------------------------------------------------------
DEVELOPMENT STYLE
------------------------------------------------------------

Implement platform conversion aggressively.

Do NOT work in tiny milestones.

Do NOT ask for testing after every subsystem.

Do NOT produce placeholder implementations unless isolated and clearly marked.

Apply the standard platform conversion work proactively.

Make architecture decisions that reduce future maintenance.

Prefer correct architecture over temporary hacks.

------------------------------------------------------------
EXPECTED OUTPUT
------------------------------------------------------------

After implementation provide only:

1. Files modified

2. Major architectural decisions

3. Remaining blockers

4. Exact build instructions

5. SD card layout

Do NOT ask for intermediate testing.

Do NOT stop before reaching the first playable milestone whenever technically possible.
