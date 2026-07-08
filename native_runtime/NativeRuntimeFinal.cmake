# Native Nintendo Switch CPU configuration.
# Rendering is compiled directly from the Deko3D sources; this file enables the production
# AArch64 execution paths without changing system clock rates.

function(azahar_enable_native_cpu_runtime)
    if(TARGET citra_core)
        target_compile_definitions(citra_core PRIVATE
            AZAHAR_SWITCH_DYNARMIC_SAFE_OPTIMIZATIONS)
    endif()
    if(TARGET video_core)
        target_compile_definitions(video_core PRIVATE
            AZAHAR_SWITCH_PICA_SHADER_JIT)
    endif()
endfunction()

cmake_language(DEFER CALL azahar_enable_native_cpu_runtime)
