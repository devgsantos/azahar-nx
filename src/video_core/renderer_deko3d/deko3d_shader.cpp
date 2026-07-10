// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_shader.h"

#include <cstring>
#include <span>

#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "video_core/renderer_deko3d/deko3d_state.h"

#ifdef __SWITCH__
#include "renderer_deko3d/generated/deko3d_builtin_shaders.h"
#endif

namespace VideoCore::Deko3D {
namespace {

#ifdef __SWITCH__
u32 AlignUp(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}
#endif

} // namespace

bool ShaderCache::Initialize(State& state) {
    SWITCH_TRACE_EVENT("Deko3D", "ShaderCache::Initialize", "enter");
#ifdef __SWITCH__
    if (!LoadBuiltInShaders(state.GetDevice())) {
        Shutdown();
        SWITCH_TRACE_EVENT("Deko3D", "ShaderCache::Initialize", "failed");
        return false;
    }
    initialized = true;
    LOG_INFO(Render, "Deko3D shader cache initialized with built-in vertex-color DKSH programs");
#else
    initialized = true;
#endif
    SWITCH_TRACE_EVENT("Deko3D", "ShaderCache::Initialize", "leave");
    return true;
}

void ShaderCache::Shutdown() {
#ifdef __SWITCH__
    if (shader_code_mem_block) {
        dkMemBlockDestroy(shader_code_mem_block);
        shader_code_mem_block = nullptr;
    }
    shader_code_cpu = nullptr;
    shader_code_offset = 0;
    color_vertex_shader = {};
    color_fragment_shader = {};
    color_vertex_shader_valid = false;
    color_fragment_shader_valid = false;
    tex_vertex_shader = {};
    tex_fragment_shader = {};
    tex_vertex_shader_valid = false;
    tex_fragment_shader_valid = false;
    present_vertex_shader = {};
    present_fragment_shader = {};
    present_vertex_shader_valid = false;
    present_fragment_shader_valid = false;
#endif
    initialized = false;
}

#ifdef __SWITCH__
bool ShaderCache::LoadBuiltInShaders(DkDevice device) {
    if (!device) {
        LOG_ERROR(Render, "Deko3D shader cache cannot initialize without a device");
        return false;
    }

    const u32 code_size =
        DK_SHADER_CODE_UNUSABLE_SIZE +
        AlignUp(static_cast<u32>(BuiltinShaders::PicaColorVshSize), DK_SHADER_CODE_ALIGNMENT) +
        AlignUp(static_cast<u32>(BuiltinShaders::PicaColorFshSize), DK_SHADER_CODE_ALIGNMENT) +
        AlignUp(static_cast<u32>(BuiltinShaders::PicaTexVshSize), DK_SHADER_CODE_ALIGNMENT) +
        AlignUp(static_cast<u32>(BuiltinShaders::PicaTexFshSize), DK_SHADER_CODE_ALIGNMENT) +
        AlignUp(static_cast<u32>(BuiltinShaders::PresentVshSize), DK_SHADER_CODE_ALIGNMENT) +
        AlignUp(static_cast<u32>(BuiltinShaders::PresentFshSize), DK_SHADER_CODE_ALIGNMENT);

    DkMemBlockMaker shader_mem_maker;
    dkMemBlockMakerDefaults(&shader_mem_maker, device, AlignUp(code_size, DK_MEMBLOCK_ALIGNMENT));
    shader_mem_maker.flags =
        DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_Code;
    shader_code_mem_block = dkMemBlockCreate(&shader_mem_maker);
    if (!shader_code_mem_block) {
        LOG_ERROR(Render, "Deko3D shader code memory allocation failed");
        return false;
    }
    shader_code_cpu = dkMemBlockGetCpuAddr(shader_code_mem_block);
    if (!shader_code_cpu) {
        LOG_ERROR(Render, "Deko3D shader code memory mapping failed");
        return false;
    }

    shader_code_offset = DK_SHADER_CODE_UNUSABLE_SIZE;

    u32 vertex_offset = 0;
    if (!CopyShader({BuiltinShaders::PicaColorVsh, BuiltinShaders::PicaColorVshSize},
                    vertex_offset, color_vertex_shader)) {
        LOG_ERROR(Render, "Deko3D vertex-color vertex shader initialization failed");
        return false;
    }
    color_vertex_shader_valid = dkShaderIsValid(&color_vertex_shader);

    u32 fragment_offset = 0;
    if (!CopyShader({BuiltinShaders::PicaColorFsh, BuiltinShaders::PicaColorFshSize},
                    fragment_offset, color_fragment_shader)) {
        LOG_ERROR(Render, "Deko3D vertex-color fragment shader initialization failed");
        return false;
    }
    color_fragment_shader_valid = dkShaderIsValid(&color_fragment_shader);

    u32 tex_vertex_offset = 0;
    if (!CopyShader({BuiltinShaders::PicaTexVsh, BuiltinShaders::PicaTexVshSize},
                    tex_vertex_offset, tex_vertex_shader)) {
        LOG_ERROR(Render, "Deko3D textured vertex shader initialization failed");
        return false;
    }
    tex_vertex_shader_valid = dkShaderIsValid(&tex_vertex_shader);

    u32 tex_fragment_offset = 0;
    if (!CopyShader({BuiltinShaders::PicaTexFsh, BuiltinShaders::PicaTexFshSize},
                    tex_fragment_offset, tex_fragment_shader)) {
        LOG_ERROR(Render, "Deko3D textured fragment shader initialization failed");
        return false;
    }
    tex_fragment_shader_valid = dkShaderIsValid(&tex_fragment_shader);

    u32 present_vertex_offset = 0;
    if (!CopyShader({BuiltinShaders::PresentVsh, BuiltinShaders::PresentVshSize},
                    present_vertex_offset, present_vertex_shader)) {
        LOG_ERROR(Render, "Deko3D present vertex shader initialization failed");
        return false;
    }
    present_vertex_shader_valid = dkShaderIsValid(&present_vertex_shader);

    u32 present_fragment_offset = 0;
    if (!CopyShader({BuiltinShaders::PresentFsh, BuiltinShaders::PresentFshSize},
                    present_fragment_offset, present_fragment_shader)) {
        LOG_ERROR(Render, "Deko3D present fragment shader initialization failed");
        return false;
    }
    present_fragment_shader_valid = dkShaderIsValid(&present_fragment_shader);

    if (!color_vertex_shader_valid || !color_fragment_shader_valid || !tex_vertex_shader_valid ||
        !tex_fragment_shader_valid || !present_vertex_shader_valid ||
        !present_fragment_shader_valid) {
        LOG_ERROR(Render,
                  "Deko3D built-in shader validation failed color_v={} color_f={} tex_v={} "
                  "tex_f={} present_v={} present_f={}",
                  color_vertex_shader_valid, color_fragment_shader_valid, tex_vertex_shader_valid,
                  tex_fragment_shader_valid, present_vertex_shader_valid,
                  present_fragment_shader_valid);
        return false;
    }

    LOG_INFO(Render,
             "Deko3D built-in shaders loaded color_v={} color_f={} tex_v={} tex_f={} "
             "present_v={} present_f={}",
             vertex_offset, fragment_offset, tex_vertex_offset, tex_fragment_offset,
             present_vertex_offset, present_fragment_offset);
    return true;
}

bool ShaderCache::CopyShader(std::span<const u8> shader_code, u32& offset_out,
                             DkShader& shader_out) {
    if (shader_code.empty() || !shader_code_cpu || !shader_code_mem_block) {
        return false;
    }

    const u32 aligned_offset = AlignUp(shader_code_offset, DK_SHADER_CODE_ALIGNMENT);
    std::memcpy(static_cast<u8*>(shader_code_cpu) + aligned_offset, shader_code.data(),
                shader_code.size());

    DkShaderMaker shader_maker;
    dkShaderMakerDefaults(&shader_maker, shader_code_mem_block, aligned_offset);
    dkShaderInitialize(&shader_out, &shader_maker);

    offset_out = aligned_offset;
    shader_code_offset =
        aligned_offset + AlignUp(static_cast<u32>(shader_code.size()), DK_SHADER_CODE_ALIGNMENT);
    return true;
}
#endif

} // namespace VideoCore::Deko3D
