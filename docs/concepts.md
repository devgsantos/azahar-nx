You are creating a native Nintendo Switch fork of Azahar.

This is NOT a new emulator.

This is NOT a rewrite.

This is NOT an experimental prototype.

The project is a long-term maintained fork whose purpose is to bring Azahar to Nintendo Switch while staying as close as possible to the upstream architecture.

Whenever possible, reuse upstream Azahar code instead of rewriting it.

Every decision should prioritize long-term maintainability and the ability to merge future upstream changes with minimal conflicts.

The emulator core remains Azahar.

Only the platform layer should significantly diverge.

------------------------------------------------------------
PROJECT PHILOSOPHY
------------------------------------------------------------

The project architecture should be:

                Upstream Azahar
                      │
         ─────────────┼─────────────
                      │
              Emulator Core
                      │
         CPU / Memory / Kernel
         HLE / Loader / FS
         Services / DSP
         PICA200 / VideoCore
         Config / Saves
                      │
         Renderer Abstraction
                      │
          Nintendo Switch Layer
                      │
      Deko3D
      HID
      audren
      romfs
      sdmc
      Horizon OS
      NRO frontend

The Switch fork should replace only the platform-specific components.

Everything that belongs to emulation logic should remain identical to upstream whenever practical.

------------------------------------------------------------
CORE PRINCIPLES
------------------------------------------------------------

Always prefer:

Reuse > Rewrite

Abstraction > Platform hacks

Platform backend > Emulator modifications

Small upstream-compatible changes > Large invasive changes

Long-term maintainability > Quick hacks

Future upstream mergeability > One-off optimizations

------------------------------------------------------------
WHAT MUST REMAIN IDENTICAL TO UPSTREAM
------------------------------------------------------------

Whenever technically possible, preserve:

• CPU emulation
• ARM interpreter
• ARM JIT architecture
• Memory system
• Kernel
• Scheduler
• Loader
• Filesystem logic
• Save management
• Config system
• HLE services
• DSP
• Shader compiler
• Texture cache
• Shader cache
• Game database
• Cheats
• RTC
• Network layer
• RomFS
• ExeFS
• NCSD
• NCCH
• CIA loader
• 3DSX loader
• Crypto
• Save states

If any of these require changes, isolate them behind platform abstractions whenever possible.

------------------------------------------------------------
WHAT SHOULD BECOME SWITCH-SPECIFIC
------------------------------------------------------------

Replace desktop implementations with native Switch implementations:

• Frontend
• Window system
• Input
• Audio
• Filesystem paths
• Renderer backend
• Thread priorities
• Memory allocation
• Executable memory management
• Timing
• File browser
• Logging backend

------------------------------------------------------------
RENDERING PHILOSOPHY
------------------------------------------------------------

Do not rewrite VideoCore.

Do not rewrite PICA200.

Do not rewrite GPU emulation.

Instead:

Game
 ↓
PICA200
 ↓
VideoCore
 ↓
Renderer Interface
 ↓
Deko3D Backend

Only the renderer backend should become Switch-specific.

------------------------------------------------------------
JIT PHILOSOPHY
------------------------------------------------------------

Do not redesign Azahar's JIT.

Adapt it to Horizon OS.

Implement only the platform-specific pieces:

• executable memory
• page permissions
• cache flushing
• address space
• synchronization

The JIT architecture itself should remain Azahar's.

------------------------------------------------------------
FRONTEND PHILOSOPHY
------------------------------------------------------------

The desktop Qt frontend should not be ported.

Instead create a lightweight Switch-native frontend responsible only for:

• ROM browser
• Boot status
• Fatal errors
• FPS overlay
• Minimal settings

Nothing more.

------------------------------------------------------------
PERFORMANCE PHILOSOPHY
------------------------------------------------------------

Performance should come primarily from:

• proper renderer backend
• JIT
• cache usage
• memory management
• threading
• avoiding unnecessary copies

NOT from changing emulator logic.

------------------------------------------------------------
WHEN MODIFYING THE CORE
------------------------------------------------------------

Before modifying emulator code, ask internally:

"Can this be solved in the Switch platform layer?"

If yes:

Do not modify emulator logic.

If no:

Implement the smallest possible upstream-friendly change.

------------------------------------------------------------
END GOAL
------------------------------------------------------------

The finished project should feel like an official Nintendo Switch platform supported by Azahar.

A developer familiar with Azahar should immediately recognize the project structure.

Most commits should be platform-layer additions rather than emulator rewrites.

Future upstream merges should be as straightforward as possible.

The Switch version should simply become another supported platform for Azahar.