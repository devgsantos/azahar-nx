# Native CPU configuration for the Switch runtime branch.
# The Deko3D backend is compiled directly by src/video_core/CMakeLists.txt; no header overlays or
# renderer substitutions are used here.

function(azahar_enable_native_cpu_runtime)
    if(TARGET citra_core)
        target_compile_definitions(citra_core PRIVATE
            AZAHAR_SWITCH_DYNARMIC_SAFE_OPTIMIZATIONS)
    endif()
endfunction()

cmake_language(DEFER CALL azahar_enable_native_cpu_runtime)
