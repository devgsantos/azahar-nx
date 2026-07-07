// Copyright 2015 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/arch.h"
#include <cstdio>

#include "video_core/shader/shader_interpreter.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
#include "video_core/shader/shader_jit.h"
#endif
#include "video_core/shader/shader.h"

namespace Pica {

std::unique_ptr<ShaderEngine> CreateEngine(bool use_jit) {
#if defined(__SWITCH__) && !defined(AZAHAR_SWITCH_PICA_SHADER_JIT)
    if (use_jit) {
        static bool logged_shader_jit_disabled = false;
        if (!logged_shader_jit_disabled) {
            std::fprintf(stderr,
                         "[Switch.PICA] AArch64 shader JIT disabled for B0 crash baseline; "
                         "using interpreter\n");
            std::fflush(stderr);
            logged_shader_jit_disabled = true;
        }
    }
    return std::make_unique<Shader::InterpreterEngine>();
#else
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
    if (use_jit) {
#if defined(__SWITCH__)
        static bool logged_shader_jit_active = false;
        if (!logged_shader_jit_active) {
            std::fprintf(stderr, "[Switch.PICA] AArch64 shader JIT active\n");
            std::fflush(stderr);
            logged_shader_jit_active = true;
        }
#endif
        return std::make_unique<Shader::JitEngine>();
    }
#endif

    return std::make_unique<Shader::InterpreterEngine>();
#endif
}

} // namespace Pica
