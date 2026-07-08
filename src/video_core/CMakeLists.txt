add_subdirectory(host_shaders)

add_library(video_core STATIC EXCLUDE_FROM_ALL
    custom_textures/custom_format.cpp
    custom_textures/custom_format.h
    custom_textures/custom_tex_manager.cpp
    custom_textures/custom_tex_manager.h
    custom_textures/material.cpp
    custom_textures/material.h
    debug_utils/debug_utils.cpp
    debug_utils/debug_utils.h
    gpu.cpp
    gpu.h
    gpu_debugger.h
    pica_types.h
    precompiled_headers.h
    rasterizer_accelerated.cpp
    rasterizer_accelerated.h
    rasterizer_interface.h
    renderer_base.cpp
    renderer_base.h
    pica/dirty_regs.h
    pica/geometry_pipeline.cpp
    pica/geometry_pipeline.h
    pica/pica_core.cpp
    pica/pica_core.h
    pica/output_vertex.cpp
    pica/output_vertex.h
    pica/primitive_assembly.cpp
    pica/primitive_assembly.h
    pica/regs_external.h
    pica/regs_framebuffer.h
    pica/regs_internal.cpp
    pica/regs_internal.h
    pica/regs_lcd.h
    pica/regs_lighting.h
    pica/regs_pipeline.h
    pica/regs_rasterizer.h
    pica/regs_shader.h
    pica/regs_texturing.h
    pica/shader_setup.cpp
    pica/shader_setup.h
    pica/shader_unit.cpp
    pica/shader_unit.h
    pica/packed_attribute.h
    pica/vertex_loader.cpp
    pica/vertex_loader.h
    rasterizer_cache/framebuffer_base.h
    rasterizer_cache/pixel_format.cpp
    rasterizer_cache/pixel_format.h
    rasterizer_cache/rasterizer_cache.cpp
    rasterizer_cache/rasterizer_cache.h
    rasterizer_cache/rasterizer_cache_base.h
    rasterizer_cache/sampler_params.h
    rasterizer_cache/slot_id.h
    rasterizer_cache/surface_base.cpp
    rasterizer_cache/surface_base.h
    rasterizer_cache/surface_params.cpp
    rasterizer_cache/surface_params.h
    rasterizer_cache/texture_codec.h
    rasterizer_cache/texture_cube.h
    rasterizer_cache/utils.cpp
    rasterizer_cache/utils.h
    renderer_software/sw_blitter.cpp
    renderer_software/sw_blitter.h
    right_eye_disabler.cpp
    right_eye_disabler.h
    shader/debug_data.h
    shader/generator/glsl_fs_shader_gen.cpp
    shader/generator/glsl_fs_shader_gen.h
    shader/generator/glsl_shader_decompiler.cpp
    shader/generator/glsl_shader_decompiler.h
    shader/generator/glsl_shader_gen.cpp
    shader/generator/glsl_shader_gen.h
    shader/generator/pica_fs_config.cpp
    shader/generator/pica_fs_config.h
    shader/generator/profile.h
    shader/generator/shader_gen.cpp
    shader/generator/shader_gen.h
    shader/generator/shader_uniforms.cpp
    shader/generator/shader_uniforms.h
    shader/shader.cpp
    shader/shader.h
    shader/shader_interpreter.cpp
    shader/shader_interpreter.h
    shader/shader_jit.cpp
    shader/shader_jit.h
    shader/shader_jit_a64_compiler.cpp
    shader/shader_jit_a64_compiler.h
    shader/shader_jit_x64_compiler.cpp
    shader/shader_jit_x64_compiler.h
    texture/etc1.cpp
    texture/etc1.h
    texture/texture_decode.cpp
    texture/texture_decode.h
    utils.h
    video_core.cpp
    video_core.h
)

if (ENABLE_SOFTWARE_RENDERER)
    target_sources(video_core PRIVATE
        renderer_software/renderer_software.cpp
        renderer_software/renderer_software.h
        renderer_software/sw_clipper.cpp
        renderer_software/sw_clipper.h
        renderer_software/sw_framebuffer.cpp
        renderer_software/sw_framebuffer.h
        renderer_software/sw_lighting.cpp
        renderer_software/sw_lighting.h
        renderer_software/sw_proctex.cpp
        renderer_software/sw_proctex.h
        renderer_software/sw_rasterizer.cpp
        renderer_software/sw_rasterizer.h
        renderer_software/sw_texturing.cpp
        renderer_software/sw_texturing.h
    )
endif()

if (ENABLE_SWITCH_FRONTEND)
    set(DEKO3D_SHADER_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}/renderer_deko3d/generated")
    set(DEKO3D_SHADER_HEADER "${DEKO3D_SHADER_GEN_DIR}/deko3d_builtin_shaders.h")
    set(DEKO3D_COLOR_VSH "${DEKO3D_SHADER_GEN_DIR}/pica_color_vsh.dksh")
    set(DEKO3D_COLOR_FSH "${DEKO3D_SHADER_GEN_DIR}/pica_color_fsh.dksh")

    target_sources(video_core PRIVATE
        renderer_deko3d/deko3d_presenter.cpp
        renderer_deko3d/deko3d_presenter.h
        renderer_deko3d/deko3d_rasterizer_draw.cpp
        renderer_deko3d/deko3d_transfer_native.cpp
        renderer_deko3d/deko3d_rasterizer.h
        renderer_deko3d/deko3d_shader.cpp
        renderer_deko3d/deko3d_shader.h
        renderer_deko3d/deko3d_stats.cpp
        renderer_deko3d/deko3d_stats.h
        renderer_deko3d/deko3d_state_native.cpp
        renderer_deko3d/deko3d_state.h
        renderer_deko3d/deko3d_texture_cache.cpp
        renderer_deko3d/deko3d_texture_cache.h
        renderer_deko3d/shaders/pica_color_vsh.glsl
        renderer_deko3d/shaders/pica_color_fsh.glsl
        ${DEKO3D_SHADER_HEADER}
        renderer_deko3d/renderer_deko3d.cpp
        renderer_deko3d/renderer_deko3d.h
    )
    target_compile_definitions(video_core PUBLIC
        AZAHAR_ENABLE_DEKO3D
        AZAHAR_DEKO3D_NATIVE_RUNTIME
        AZAHAR_DEKO3D_STRICT_NATIVE)
    if(CMAKE_SYSTEM_NAME STREQUAL "NintendoSwitch")
        find_path(DEKO3D_INCLUDE_DIR deko3d.h HINTS "$ENV{DEVKITPRO}/libnx/include")
        if(AZAHAR_DEKO3D_VALIDATION)
            find_library(DEKO3D_LIBRARY deko3dd HINTS "$ENV{DEVKITPRO}/libnx/lib")
        else()
            find_library(DEKO3D_LIBRARY deko3d HINTS "$ENV{DEVKITPRO}/libnx/lib")
        endif()
        find_program(UAM_EXECUTABLE uam HINTS "$ENV{DEVKITPRO}/tools/bin")
        if(NOT DEKO3D_INCLUDE_DIR OR NOT DEKO3D_LIBRARY)
            message(FATAL_ERROR "Deko3D headers/libs are required for ENABLE_SWITCH_FRONTEND")
        endif()
        if(NOT UAM_EXECUTABLE)
            message(FATAL_ERROR "uam is required to build Deko3D shader binaries")
        endif()
        add_custom_command(
            OUTPUT ${DEKO3D_SHADER_HEADER}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${DEKO3D_SHADER_GEN_DIR}
            COMMAND ${UAM_EXECUTABLE} -s vert -o ${DEKO3D_COLOR_VSH}
                    ${CMAKE_CURRENT_SOURCE_DIR}/renderer_deko3d/shaders/pica_color_vsh.glsl
            COMMAND ${UAM_EXECUTABLE} -s frag -o ${DEKO3D_COLOR_FSH}
                    ${CMAKE_CURRENT_SOURCE_DIR}/renderer_deko3d/shaders/pica_color_fsh.glsl
            COMMAND ${CMAKE_COMMAND}
                    -DOUTPUT=${DEKO3D_SHADER_HEADER}
                    -DVERT=${DEKO3D_COLOR_VSH}
                    -DFRAG=${DEKO3D_COLOR_FSH}
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/renderer_deko3d/EmbedDeko3DShaders.cmake
            DEPENDS
                ${CMAKE_CURRENT_SOURCE_DIR}/renderer_deko3d/shaders/pica_color_vsh.glsl
                ${CMAKE_CURRENT_SOURCE_DIR}/renderer_deko3d/shaders/pica_color_fsh.glsl
                ${CMAKE_CURRENT_SOURCE_DIR}/renderer_deko3d/EmbedDeko3DShaders.cmake
            VERBATIM)
        add_custom_target(deko3d_builtin_shaders DEPENDS ${DEKO3D_SHADER_HEADER})
        add_dependencies(video_core deko3d_builtin_shaders)
        target_include_directories(video_core PRIVATE ${DEKO3D_INCLUDE_DIR})
        target_include_directories(video_core PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
        target_link_libraries(video_core PUBLIC ${DEKO3D_LIBRARY})
        if(AZAHAR_DEKO3D_VALIDATION)
            target_compile_definitions(video_core PUBLIC AZAHAR_DEKO3D_VALIDATION)
            message(STATUS "Deko3D validation enabled; linking debug validation library")
        endif()
        if(AZAHAR_DEKO3D_VERBOSE_TELEMETRY)
            target_compile_definitions(video_core PUBLIC AZAHAR_DEKO3D_VERBOSE_TELEMETRY)
            message(STATUS "Deko3D verbose telemetry enabled")
        endif()
    endif()
endif()

if (ENABLE_OPENGL)
    target_sources(video_core PRIVATE
        renderer_opengl/frame_dumper_opengl.cpp
        renderer_opengl/frame_dumper_opengl.h
        renderer_opengl/gl_blit_helper.cpp
        renderer_opengl/gl_blit_helper.h
        renderer_opengl/gl_driver.cpp
        renderer_opengl/gl_driver.h
        renderer_opengl/gl_rasterizer.cpp
        renderer_opengl/gl_rasterizer.h
        renderer_opengl/gl_rasterizer_cache.cpp
        renderer_opengl/gl_resource_manager.cpp
        renderer_opengl/gl_resource_manager.h
        renderer_opengl/gl_shader_disk_cache.cpp
        renderer_opengl/gl_shader_disk_cache.h
        renderer_opengl/gl_shader_manager.cpp
        renderer_opengl/gl_shader_manager.h
        renderer_opengl/gl_shader_util.cpp
        renderer_opengl/gl_shader_util.h
        renderer_opengl/gl_state.cpp
        renderer_opengl/gl_state.h
        renderer_opengl/gl_stream_buffer.cpp
        renderer_opengl/gl_stream_buffer.h
        renderer_opengl/gl_texture_mailbox.cpp
        renderer_opengl/gl_texture_mailbox.h
        renderer_opengl/gl_texture_runtime.cpp
        renderer_opengl/gl_texture_runtime.h
        renderer_opengl/gl_vars.cpp
        renderer_opengl/gl_vars.h
        renderer_opengl/pica_to_gl.h
        renderer_opengl/post_processing_opengl.cpp
        renderer_opengl/post_processing_opengl.h
        renderer_opengl/renderer_opengl.cpp
        renderer_opengl/renderer_opengl.h
    )
    target_link_libraries(video_core PRIVATE glad)
endif()
