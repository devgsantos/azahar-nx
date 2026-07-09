// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <span>

#include "common/common_types.h"

#ifdef __SWITCH__
#include <deko3d.h>
#endif

namespace VideoCore::Deko3D {

class State;

class ShaderCache {
public:
    bool Initialize(State& state);
    void Shutdown();
    bool IsInitialized() const {
        return initialized;
    }

#ifdef __SWITCH__
    [[nodiscard]] const DkShader* GetColorVertexShader() const {
        return color_vertex_shader_valid ? &color_vertex_shader : nullptr;
    }

    [[nodiscard]] const DkShader* GetColorFragmentShader() const {
        return color_fragment_shader_valid ? &color_fragment_shader : nullptr;
    }

    [[nodiscard]] const DkShader* GetTexVertexShader() const {
        return tex_vertex_shader_valid ? &tex_vertex_shader : nullptr;
    }

    [[nodiscard]] const DkShader* GetTexFragmentShader() const {
        return tex_fragment_shader_valid ? &tex_fragment_shader : nullptr;
    }
#endif

private:
#ifdef __SWITCH__
    bool LoadBuiltInShaders(DkDevice device);
    bool CopyShader(std::span<const u8> shader_code, u32& offset_out, DkShader& shader_out);

    DkMemBlock shader_code_mem_block{};
    void* shader_code_cpu = nullptr;
    u32 shader_code_offset = 0;
    DkShader color_vertex_shader{};
    DkShader color_fragment_shader{};
    bool color_vertex_shader_valid = false;
    bool color_fragment_shader_valid = false;
    DkShader tex_vertex_shader{};
    DkShader tex_fragment_shader{};
    bool tex_vertex_shader_valid = false;
    bool tex_fragment_shader_valid = false;
#endif

    bool initialized = false;
};

} // namespace VideoCore::Deko3D
