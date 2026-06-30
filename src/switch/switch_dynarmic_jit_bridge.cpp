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
#include <cstring>
#include <new>

namespace {

constexpr const char* LogPath = "sdmc:/switch/azahar/logs/azahar-switch-early.log";
constexpr std::size_t MaxRegisteredJitRanges = 64;

struct SwitchDynarmicJit {
    Jit jit{};
    std::uint32_t id = 0;
};

struct RegisteredJitRange {
    std::uintptr_t rw = 0;
    std::uintptr_t rx = 0;
    std::size_t size = 0;
    std::uint32_t id = 0;
};

struct JitBreadcrumb {
    std::uint32_t phase = 0;
    std::uint32_t svc = 0;
    std::uintptr_t sp = 0;
    std::uintptr_t lr = 0;
    std::uintptr_t x16 = 0;
    std::uintptr_t x17 = 0;
};

RegisteredJitRange registered_ranges[MaxRegisteredJitRanges]{};
JitBreadcrumb breadcrumb{};
std::uint32_t next_jit_id = 1;

void Log(const char* format, ...) noexcept {
    std::FILE* file = std::fopen(LogPath, "a");
    va_list args;
    va_start(args, format);
    va_list file_args;
    va_copy(file_args, args);

    std::fprintf(stderr, "[Dynarmic.JIT] ");
    std::vfprintf(stderr, format, args);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);

    if (file != nullptr) {
        std::fputs("[Dynarmic.JIT] ", file);
        std::vfprintf(file, format, file_args);
        std::fputc('\n', file);
        std::fflush(file);
        std::fclose(file);
    }
    va_end(file_args);
    va_end(args);
}

void RegisterRange(SwitchDynarmicJit& handle, std::uint32_t* rw,
                   std::uint32_t* rx) noexcept {
    handle.id = next_jit_id++;
    if (handle.id == 0) {
        handle.id = next_jit_id++;
    }

    for (auto& range : registered_ranges) {
        if (range.size == 0) {
            range = {
                reinterpret_cast<std::uintptr_t>(rw),
                reinterpret_cast<std::uintptr_t>(rx),
                handle.jit.size,
                handle.id,
            };
            return;
        }
    }

    Log("range registry full id=%u rw=%p rx=%p size=%zu", handle.id,
        static_cast<void*>(rw), static_cast<void*>(rx), handle.jit.size);
}

void UnregisterRange(const SwitchDynarmicJit& handle) noexcept {
    for (auto& range : registered_ranges) {
        if (range.id == handle.id) {
            range = {};
            return;
        }
    }
}

const char* DescribeAddress(std::uintptr_t address, char* buffer,
                            std::size_t buffer_size) noexcept {
    if (buffer == nullptr || buffer_size == 0) {
        return "";
    }

    for (const auto& range : registered_ranges) {
        if (range.size == 0) {
            continue;
        }

        const auto rw_end = range.rw + range.size;
        const auto rx_end = range.rx + range.size;
        if (address >= range.rw && address < rw_end) {
            const auto offset = address - range.rw;
            std::snprintf(buffer, buffer_size,
                          "RW alias id=%u offset=0x%zx rw=0x%016llx rx=0x%016llx size=0x%zx",
                          range.id, static_cast<std::size_t>(offset),
                          static_cast<unsigned long long>(range.rw),
                          static_cast<unsigned long long>(range.rx), range.size);
            return buffer;
        }
        if (address >= range.rx && address < rx_end) {
            const auto offset = address - range.rx;
            std::snprintf(buffer, buffer_size,
                          "RX alias id=%u offset=0x%zx rw=0x%016llx rx=0x%016llx size=0x%zx",
                          range.id, static_cast<std::size_t>(offset),
                          static_cast<unsigned long long>(range.rw),
                          static_cast<unsigned long long>(range.rx), range.size);
            return buffer;
        }
    }

    std::snprintf(buffer, buffer_size, "outside registered Dynarmic JIT ranges");
    return buffer;
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
    RegisterRange(*handle, *rw, *rx);
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
    UnregisterRange(*handle);
    delete handle;
}

extern "C" const char* azahar_switch_dynarmic_jit_describe_address(
    std::uintptr_t address, char* buffer, std::size_t buffer_size) noexcept {
    return DescribeAddress(address, buffer, buffer_size);
}

extern "C" void azahar_switch_dynarmic_jit_svc_phase(
    std::uint32_t phase, std::uint32_t svc, std::uintptr_t lr,
    std::uintptr_t x16, std::uintptr_t x17) noexcept {
    std::uintptr_t sp = 0;
#if defined(__aarch64__)
    __asm__ volatile("mov %0, sp" : "=r"(sp));
#endif
    breadcrumb = {
        phase,
        svc,
        sp,
        lr,
        x16,
        x17,
    };
}

extern "C" void azahar_switch_dynarmic_jit_get_breadcrumb(
    JitBreadcrumb* out) noexcept {
    if (out != nullptr) {
        std::memcpy(out, &breadcrumb, sizeof(*out));
    }
}
