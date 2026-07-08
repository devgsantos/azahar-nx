function(azahar_apply_native_state_header)
    if(TARGET video_core)
        target_precompile_headers(video_core PRIVATE
            "$<$<COMPILE_LANGUAGE:CXX>:${CMAKE_SOURCE_DIR}/src/video_core/renderer_deko3d/deko3d_native_overrides.h>")
    endif()
endfunction()
cmake_language(DEFER CALL azahar_apply_native_state_header)
