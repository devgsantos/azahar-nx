add_executable(azahar_switch
    switch_app.cpp
    switch_app.h
    switch_audio.cpp
    switch_audio.h
    switch_debug_log.cpp
    switch_debug_log.h
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
)

create_target_directory_groups(azahar_switch)

target_compile_definitions(azahar_switch PRIVATE AZAHAR_SWITCH)
target_link_libraries(azahar_switch PRIVATE citra_common citra_core video_core audio_core input_common)

if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
    target_link_libraries(azahar_switch PRIVATE nx)
endif()

find_program(ELF2NRO elf2nro HINTS "$ENV{DEVKITPRO}/tools/bin")
if(ELF2NRO)
    add_custom_command(TARGET azahar_switch POST_BUILD
        COMMAND ${ELF2NRO} $<TARGET_FILE:azahar_switch> ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/azahar.nro
        COMMENT "Packaging azahar.nro")
endif()
