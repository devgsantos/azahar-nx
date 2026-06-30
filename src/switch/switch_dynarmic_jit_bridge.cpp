// SPDX-License-Identifier: GPL-2.0-or-later
// AZAHAR_SWITCH_DUAL_ALIAS_JIT_V3
//
// libnx-only bridge for Dynarmic/Oaknut JIT memory. Do not include Azahar
// common headers here: both projects define an incompatible global u128 type.

#include <switch.h>

#include <algorithm>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>

namespace {

constexpr const char* LogPath = "sdmc:/switch/azahar/logs/azahar-switch-early.log";

struct SwitchDynarmicJit {
    Jit jit{};
};

void Log(const char* format, ...) noexcept {
    std::FILE* file = std::fopen(LogPath, "a");
    if (file == nullptr) {
        return;
    }

    std::fputs("[Dynarmic.JIT] ", file);
    va_list args;
    va_start(args, format);
    std::vfprintf(file, format, args);
    va_end(args);
    std::fputc('\n', file);
    std::fflush(file);
    std::fclose(file);
}

}  // namespace

extern "C" void* azahar_switch_dynarmic_jit_create(std::size_t size,
                                                     std::uint32_t** rw,
                                                     std::uint32_t** rx) noexcept {
    if (rw == nullptr || rx == nullptr || size == 0) {
        return nullptr;
    }

    *rw = nullptr;
    *rx = nullptr;

    auto* handle = new (std::nothrow) SwitchDynarmicJit{};
    if (handle == nullptr) {
        return nullptr;
    }

    const Result rc = jitCreate(&handle->jit, size);
    if (R_FAILED(rc)) {
        Log("jitCreate failed rc=0x%08x requested_size=%zu", rc, size);
        delete handle;
        return nullptr;
    }

    *rw = static_cast<std::uint32_t*>(jitGetRwAddr(&handle->jit));
    *rx = static_cast<std::uint32_t*>(jitGetRxAddr(&handle->jit));
    if (*rw == nullptr || *rx == nullptr) {
        Log("jitCreate returned null alias type=%d rw=%p rx=%p",
            static_cast<int>(handle->jit.type), static_cast<void*>(*rw),
            static_cast<void*>(*rx));
        jitClose(&handle->jit);
        delete handle;
        *rw = nullptr;
        *rx = nullptr;
        return nullptr;
    }

    Log("created type=%d requested_size=%zu actual_size=%zu rw=%p rx=%p",
        static_cast<int>(handle->jit.type), size, handle->jit.size,
        static_cast<void*>(*rw), static_cast<void*>(*rx));
    return handle;
}

extern "C" bool azahar_switch_dynarmic_jit_begin_write(void* opaque) noexcept {
    auto* handle = static_cast<SwitchDynarmicJit*>(opaque);
    if (handle == nullptr) {
        return false;
    }

    // CodeMemory has simultaneous RW and RX aliases. The permission-based
    // fallback must unmap the RX view before writes resume.
    if (handle->jit.type == JitType_CodeMemory) {
        return true;
    }

    const Result rc = jitTransitionToWritable(&handle->jit);
    if (R_FAILED(rc)) {
        Log("jitTransitionToWritable failed rc=0x%08x", rc);
        return false;
    }
    return true;
}

extern "C" bool azahar_switch_dynarmic_jit_end_write(void* opaque,
                                                      std::size_t offset,
                                                      std::size_t size) noexcept {
    auto* handle = static_cast<SwitchDynarmicJit*>(opaque);
    if (handle == nullptr) {
        return false;
    }

    const std::size_t total_size = handle->jit.size;
    offset = std::min(offset, total_size);
    size = std::min(size, total_size - offset);

    auto* rw = static_cast<std::uint8_t*>(jitGetRwAddr(&handle->jit));
    auto* rx = static_cast<std::uint8_t*>(jitGetRxAddr(&handle->jit));

    if (size != 0) {
        armDCacheFlush(rw + offset, size);
    }

    if (handle->jit.type == JitType_SetProcessMemoryPermission) {
        const Result rc = jitTransitionToExecutable(&handle->jit);
        if (R_FAILED(rc)) {
            Log("jitTransitionToExecutable failed rc=0x%08x offset=%zu size=%zu",
                rc, offset, size);
            return false;
        }
    }

    // For CodeMemory, avoid libnx's whole-buffer flush on every emitted block:
    // both aliases are permanently mapped, so range maintenance is sufficient.
    // For the permission fallback, invalidate after the RX view is mapped.
    if (size != 0) {
        armICacheInvalidate(rx + offset, size);
    }

    return true;
}

extern "C" void azahar_switch_dynarmic_jit_destroy(void* opaque) noexcept {
    auto* handle = static_cast<SwitchDynarmicJit*>(opaque);
    if (handle == nullptr) {
        return;
    }

    const Result rc = jitClose(&handle->jit);
    if (R_FAILED(rc)) {
        Log("jitClose failed rc=0x%08x", rc);
    }
    delete handle;
}
