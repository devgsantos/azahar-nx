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
