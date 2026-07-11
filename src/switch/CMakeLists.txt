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

# A software fallback marks its guest-memory render target CPU-dirty. Without a resolve, every later
# otherwise-supported batch is rejected forever. Restore RGBA8 guest contents into the cached Deko3D
# image at the software-to-hardware handoff. Keep a build-time kill switch for hardware rollback.
option(AZAHAR_SWITCH_DEKO3D_CPU_DIRTY_RESOLVE
       "Resolve CPU-dirty RGBA8 render targets before Deko3D hardware draws" ON)
if(AZAHAR_SWITCH_DEKO3D_CPU_DIRTY_RESOLVE)
    target_sources(video_core PRIVATE
        ${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/deko3d_cpu_dirty_resolve.cpp)
    # The handoff token rewrite is intentionally visible only while compiling the rasterizer source.
    # Other video_core files include the same headers and must retain the ordinary bool field.
    set_property(SOURCE
        ${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/deko3d_rasterizer.cpp
        TARGET_DIRECTORY video_core
        APPEND PROPERTY COMPILE_DEFINITIONS AZAHAR_DEKO3D_CPU_DIRTY_HANDOFF=1)
    message(STATUS "Switch Deko3D CPU-dirty render-target resolve enabled")
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
