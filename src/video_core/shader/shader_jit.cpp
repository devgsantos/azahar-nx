// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/arch.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)

#include <cstdio>
#include <exception>

#include "common/assert.h"
#include "common/hash.h"
#include "common/logging/log.h"
#include "common/microprofile.h"
#include "video_core/shader/shader.h"
#include "video_core/shader/shader_interpreter.h"
#include "video_core/shader/shader_jit.h"
#if CITRA_ARCH(arm64)
#include "video_core/shader/shader_jit_a64_compiler.h"
#endif
#if CITRA_ARCH(x86_64)
#include "video_core/shader/shader_jit_x64_compiler.h"
#endif

namespace Pica::Shader {

JitEngine::JitEngine() : fallback(std::make_unique<InterpreterEngine>()) {}
JitEngine::~JitEngine() = default;

void JitEngine::SetupBatch(ShaderSetup& setup, u32 entry_point) {
    ASSERT(entry_point < MAX_PROGRAM_CODE_LENGTH);

    if (fallback_active) {
        setup.cached_shader = nullptr;
        fallback->SetupBatch(setup, entry_point);
        return;
    }

    setup.entry_point = entry_point;

    setup.DoProgramCodeFixup();
    const u64 code_hash = setup.GetProgramCodeHash();
    const u64 swizzle_hash = setup.GetSwizzleDataHash();

    const u64 cache_key = Common::HashCombine(code_hash, swizzle_hash);
    auto iter = cache.find(cache_key);
    if (iter != cache.end()) {
        setup.cached_shader = iter->second.get();
    } else {
        try {
            auto shader = std::make_unique<JitShader>();
            shader->Compile(&setup.GetProgramCode(), &setup.GetSwizzleData());
            setup.cached_shader = shader.get();
            cache.emplace_hint(iter, cache_key, std::move(shader));
        } catch (const std::exception& e) {
            static bool logged_compile_failure = false;
            if (!logged_compile_failure) {
                LOG_ERROR(HW_GPU,
                          "Switch PICA AArch64 shader JIT failed; falling back to interpreter: {}",
                          e.what());
                std::fprintf(stderr,
                             "[Switch.PICA] AArch64 shader JIT failed; using interpreter: %s\n",
                             e.what());
                std::fflush(stderr);
                logged_compile_failure = true;
            }
            fallback_active = true;
            cache.clear();
            setup.cached_shader = nullptr;
            fallback->SetupBatch(setup, entry_point);
        } catch (...) {
            static bool logged_compile_failure = false;
            if (!logged_compile_failure) {
                LOG_ERROR(HW_GPU,
                          "Switch PICA AArch64 shader JIT failed; falling back to interpreter");
                std::fprintf(stderr,
                             "[Switch.PICA] AArch64 shader JIT failed; using interpreter\n");
                std::fflush(stderr);
                logged_compile_failure = true;
            }
            fallback_active = true;
            cache.clear();
            setup.cached_shader = nullptr;
            fallback->SetupBatch(setup, entry_point);
        }
    }
}

MICROPROFILE_DECLARE(GPU_Shader);

void JitEngine::Run(const ShaderSetup& setup, ShaderUnit& state) const {
    if (fallback_active) {
        fallback->Run(setup, state);
        return;
    }

    ASSERT(setup.cached_shader != nullptr);

    MICROPROFILE_SCOPE(GPU_Shader);

    const JitShader* shader = static_cast<const JitShader*>(setup.cached_shader);
    shader->Run(setup, state, setup.entry_point);
}

} // namespace Pica::Shader

#endif // CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
