add_executable(azahar_switch
    switch_app.cpp
    switch_app.h
    switch_audio.cpp
    switch_audio.h
    switch_debug_log.cpp
    switch_debug_log.h
    switch_dynarmic_jit_bridge.cpp
    switch_dynarmic_fast_hooks.cpp
    switch_exception_handler.cpp
    switch_jit.cpp
    switch_jit.h
    switch_input.cpp
    switch_input.h
    switch_libnx.cpp
    switch_libnx.h
    switch_paths.cpp
    switch_paths.h
    switch_posix_compat.cpp
    switch_rom_browser.cpp
    switch_rom_browser.h
    switch_window.cpp
    switch_window.h

    switch_nxlink.cpp
    switch_nxlink.h)

create_target_directory_groups(azahar_switch)

target_compile_definitions(azahar_switch PRIVATE AZAHAR_SWITCH)
target_link_libraries(azahar_switch PRIVATE citra_common citra_core video_core audio_core input_common)

# A display transfer is a point-in-time GPU operation, not a permanent alias to a mutable render
# target. Store supported RGBA8 no-scale transfers in dedicated cached destination images. This keeps
# top/bottom presentation independent from later source ownership changes and allows the next hybrid
# milestone to retain software fallback for the packed top target while testing hardware rendering on
# independently presentable targets such as the 240x320 lower screen.
option(AZAHAR_SWITCH_DEKO3D_DISPLAY_TRANSFER_SNAPSHOTS
       "Use GPU-backed snapshots for supported Deko3D display transfers" ON)
if(AZAHAR_SWITCH_DEKO3D_DISPLAY_TRANSFER_SNAPSHOTS)
    target_sources(video_core PRIVATE
        ${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/deko3d_display_transfer_snapshot.cpp)
    # Force-include the public class declaration before renaming only the legacy definition in
    # deko3d_state.cpp. Other translation units continue linking State::RecordDisplayTransfer to the
    # snapshot implementation above without changing the class declaration or ABI.
    set_property(SOURCE
        ${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/deko3d_state.cpp
        TARGET_DIRECTORY video_core
        APPEND PROPERTY COMPILE_OPTIONS
        "-include${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/deko3d_display_transfer_override.h")
    message(STATUS "Switch Deko3D display-transfer snapshots enabled")
else()
    message(STATUS "Switch Deko3D display-transfer snapshots disabled")
endif()

# The experimental CPU-dirty handoff can move software-rendered guest contents back into Deko3D,
# but DKCR hardware testing showed that enabling it globally bypasses a correctness barrier: all
# transformed batches move to hardware while the top display-transfer target disappears, producing
# a black upper screen and a white lower screen. Keep the implementation available for narrowly
# instrumented experiments, but default stable builds to the known-correct software fallback.
option(AZAHAR_SWITCH_DEKO3D_CPU_DIRTY_RESOLVE
       "Experimentally resolve CPU-dirty RGBA8 render targets before Deko3D hardware draws" OFF)
if(AZAHAR_SWITCH_DEKO3D_CPU_DIRTY_RESOLVE)
    target_sources(video_core PRIVATE
        ${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/deko3d_cpu_dirty_resolve.cpp)
    # The handoff token rewrite is intentionally visible only while compiling the rasterizer source.
    # Other video_core files include the same headers and must retain the ordinary bool field.
    set_property(SOURCE
        ${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/deko3d_rasterizer.cpp
        TARGET_DIRECTORY video_core
        APPEND PROPERTY COMPILE_DEFINITIONS AZAHAR_DEKO3D_CPU_DIRTY_HANDOFF=1)
    message(WARNING "Experimental Switch Deko3D CPU-dirty render-target resolve enabled")
else()
    message(STATUS "Switch Deko3D CPU-dirty render-target resolve disabled (stable)")
endif()

# Dynarmic normally publishes a newly emitted block, relinks older referencing blocks, and publishes
# again. On libnx CodeMemory each publication flushes and invalidates the complete 8 MiB allocation.
# The fork already supports combining those writes into one publication without using the unstable
# partial-range path. Keep the option controlled by the Azahar integration branch.
if(AZAHAR_SWITCH_JIT_CONSOLIDATED_PUBLISH AND TARGET dynarmic)
    target_compile_definitions(dynarmic PRIVATE AZAHAR_SWITCH_DYNARMIC_CONSOLIDATED_PUBLISH)
    message(STATUS "Switch Dynarmic consolidated publication enabled")
endif()

# Keep the full crash breadcrumbs for explicit diagnostic builds. In normal performance builds,
# rename only the expensive bridge implementations so the lightweight hooks provide the public
# symbols without changing Dynarmic or the exception-handler ABI.
if(NOT AZAHAR_SWITCH_PERF_DIAGNOSTICS)
    set_property(SOURCE switch_dynarmic_jit_bridge.cpp APPEND PROPERTY COMPILE_DEFINITIONS
        azahar_switch_dynarmic_jit_set_breadcrumb_phase=azahar_switch_dynarmic_jit_set_breadcrumb_phase_diagnostic
        azahar_switch_dynarmic_jit_update_breadcrumb=azahar_switch_dynarmic_jit_update_breadcrumb_diagnostic
        azahar_switch_dynarmic_jit_log_host_timing=azahar_switch_dynarmic_jit_log_host_timing_diagnostic
        azahar_switch_dynarmic_jit_log_a32_svc=azahar_switch_dynarmic_jit_log_a32_svc_diagnostic)

    # ARM_Dynarmic::Run used to update atomic counters on every host dispatch even when the values
    # were never consumed. Redirect only citra_core's call site to the lightweight implementation.
    target_compile_definitions(citra_core PRIVATE
        azahar_switch_dynarmic_jit_record_run=azahar_switch_dynarmic_jit_record_run_noop)
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    target_link_libraries(azahar_switch PRIVATE nx)
endif()

find_program(ELF2NRO elf2nro HINTS "$ENV{DEVKITPRO}/tools/bin")
if(ELF2NRO)
    add_custom_command(TARGET azahar_switch POST_BUILD
        COMMAND ${ELF2NRO} $<TARGET_FILE:azahar_switch> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/azahar.nro
        COMMENT "Packaging azahar.nro")
endif()
