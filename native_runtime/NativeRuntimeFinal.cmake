function(azahar_enable_native_runtime_final)
    if(TARGET video_core)
        target_compile_definitions(video_core PRIVATE
            AZAHAR_DEKO3D_STRICT_NATIVE
            AZAHAR_DEKO3D_NATIVE_RUNTIME)
        target_precompile_headers(video_core PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/deko3d_native_bootstrap.h>")
    endif()

    if(TARGET citra_core)
        target_compile_definitions(citra_core PRIVATE
            AZAHAR_SWITCH_DYNARMIC_SAFE_OPTIMIZATIONS)
    endif()
endfunction()

cmake_language(DEFER CALL azahar_enable_native_runtime_final)
