// SPDX-License-Identifier: GPL-2.0-or-later
// AZAHAR_SWITCH_DUAL_ALIAS_JIT_V3
//
// libnx-only bridge for Dynarmic/Oaknut JIT memory. Do not include Azahar
// common headers here: both projects define an incompatible global u128 type.

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <new>

namespace {

constexpr const char* LogPath = "sdmc:/switch/azahar/logs/azahar-switch-early.log";
constexpr std::size_t MaxRegisteredJitRanges = 64;
constexpr const char* DefaultOwner = "Dynarmic.CodeBlock";

struct SwitchDynarmicJit {
    Jit jit{};
    std::uint32_t id = 0;
};

enum class JitAddressClass : std::uint32_t {
    Outside,
    RW,
    RX,
};

enum class JitExecutionPhase : std::uint32_t {
    None,
    BeforeBlock,
    BeforeCallback,
    InsideCallback,
    AfterCallback,
    BeforeDispatcherReturn,
    BeforeHostGetTicks,
    AfterHostGetTicks,
    BeforeGeneratedRun,
    AfterGeneratedRun,
    BeforeHostAddTicks,
    AfterHostAddTicks,
};

struct JitRange {
    std::uintptr_t rw = 0;
    std::uintptr_t rx = 0;
    std::size_t size = 0;
    const char* owner = nullptr;
    std::uint32_t id = 0;
};

struct JitAddressInfo {
    JitAddressClass address_class = JitAddressClass::Outside;
    const char* owner = nullptr;
    std::uint32_t id = 0;
    std::size_t offset = 0;
    std::uintptr_t other_alias = 0;
};

struct JitExecutionBreadcrumb {
    std::atomic<std::uint32_t> phase{static_cast<std::uint32_t>(JitExecutionPhase::None)};
    std::uintptr_t block_entry = 0;
    std::uintptr_t callback_target = 0;
    std::uintptr_t continuation = 0;
    std::uintptr_t dispatcher_target = 0;
    std::uintptr_t host_lr = 0;
    std::uintptr_t host_sp = 0;
    std::uintptr_t host_x16 = 0;
    std::uintptr_t host_x17 = 0;
    std::uint32_t guest_pc = 0;
    std::uint32_t svc = 0;
};

JitRange registered_ranges[MaxRegisteredJitRanges]{};
JitExecutionBreadcrumb breadcrumb{};
std::uint32_t next_jit_id = 1;
std::uintptr_t last_dispatcher_target = 0;
std::uintptr_t last_run_entry = 0;
std::uint32_t host_timing_log_count = 0;

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
                   std::uint32_t* rx, const char* owner) noexcept {
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
                owner != nullptr ? owner : DefaultOwner,
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

JitAddressInfo ClassifyAddress(std::uintptr_t address) noexcept {
    for (const auto& range : registered_ranges) {
        if (range.size == 0) {
            continue;
        }

        const auto rw_end = range.rw + range.size;
        const auto rx_end = range.rx + range.size;
        if (address >= range.rw && address < rw_end) {
            const auto offset = address - range.rw;
            return {
                JitAddressClass::RW,
                range.owner,
                range.id,
                static_cast<std::size_t>(offset),
                range.rx + offset,
            };
        }
        if (address >= range.rx && address < rx_end) {
            const auto offset = address - range.rx;
            return {
                JitAddressClass::RX,
                range.owner,
                range.id,
                static_cast<std::size_t>(offset),
                range.rw + offset,
            };
        }
    }

    return {};
}

const char* ClassName(JitAddressClass address_class) noexcept {
    switch (address_class) {
    case JitAddressClass::RW:
        return "RW";
    case JitAddressClass::RX:
        return "RX";
    case JitAddressClass::Outside:
    default:
        return "Outside";
    }
}

const char* DescribeAddress(std::uintptr_t address, char* buffer,
                            std::size_t buffer_size) noexcept {
    if (buffer == nullptr || buffer_size == 0) {
        return "";
    }

    const JitAddressInfo info = ClassifyAddress(address);
    if (info.address_class != JitAddressClass::Outside) {
        std::snprintf(buffer, buffer_size,
                      "%s owner=%s id=%u offset=0x%zx other_alias=0x%016llx",
                      ClassName(info.address_class),
                      info.owner != nullptr ? info.owner : "unknown", info.id, info.offset,
                      static_cast<unsigned long long>(info.other_alias));
        return buffer;
    }

    std::snprintf(buffer, buffer_size, "outside registered Dynarmic JIT ranges");
    return buffer;
}

void LogAddressFields(const char* name, std::uintptr_t address) noexcept {
    const JitAddressInfo info = ClassifyAddress(address);
    Log("%s=0x%016llx", name, static_cast<unsigned long long>(address));
    Log("%s_class=%s", name, ClassName(info.address_class));
    Log("%s_owner=%s", name, info.owner != nullptr ? info.owner : "none");
    Log("%s_id=%u", name, info.id);
    Log("%s_offset=0x%zx", name, info.offset);
    Log("%s_corresponding_alias=0x%016llx", name,
        static_cast<unsigned long long>(info.other_alias));
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

    RegisterRange(*handle, *rw, *rx, DefaultOwner);
    Log("owner=%s id=%u type=%d requested_size=%zu actual_size=%zu rw=%p rx=%p",
        DefaultOwner, handle->id, static_cast<int>(handle->jit.type), size,
        handle->jit.size, static_cast<void*>(*rw), static_cast<void*>(*rx));
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

extern "C" void azahar_switch_dynarmic_jit_classify_address(
    std::uintptr_t address, JitAddressInfo* out) noexcept {
    if (out != nullptr) {
        *out = ClassifyAddress(address);
    }
}

extern "C" void azahar_switch_dynarmic_jit_set_dispatcher_target(
    std::uintptr_t dispatcher_target) noexcept {
    last_dispatcher_target = dispatcher_target;
}

extern "C" bool azahar_switch_dynarmic_jit_is_rx_address(
    std::uintptr_t address) noexcept {
    return ClassifyAddress(address).address_class == JitAddressClass::RX;
}

extern "C" std::uint32_t azahar_switch_dynarmic_jit_get_range_id(
    std::uintptr_t address) noexcept {
    return ClassifyAddress(address).id;
}

extern "C" void azahar_switch_dynarmic_jit_log_prelude_target(
    const char* name, std::uintptr_t address) noexcept {
    const JitAddressInfo info = ClassifyAddress(address);
    if (address == 0) {
        Log("prelude name=%s address=null class=host-side-callback id=0 offset=0x0",
            name != nullptr ? name : "unknown");
        return;
    }
    Log("prelude name=%s address=0x%016llx class=%s id=%u offset=0x%zx",
        name != nullptr ? name : "unknown",
        static_cast<unsigned long long>(address), ClassName(info.address_class),
        info.id, info.offset);
}

extern "C" void azahar_switch_dynarmic_jit_log_run_entry(
    std::uintptr_t run_entry) noexcept {
    last_run_entry = run_entry;
    LogAddressFields("run_entry", run_entry);
}

extern "C" std::uintptr_t azahar_switch_dynarmic_jit_get_run_entry() noexcept {
    return last_run_entry;
}

extern "C" void azahar_switch_dynarmic_jit_set_breadcrumb_phase(
    std::uint32_t phase, std::uintptr_t block_entry, std::uint32_t guest_pc) noexcept {
    breadcrumb.block_entry = block_entry;
    breadcrumb.callback_target = 0;
    breadcrumb.continuation = 0;
    breadcrumb.dispatcher_target = last_dispatcher_target;
    breadcrumb.host_lr = 0;
    breadcrumb.host_sp = 0;
    breadcrumb.host_x16 = 0;
    breadcrumb.host_x17 = 0;
    breadcrumb.guest_pc = guest_pc;
    breadcrumb.svc = 0;
    breadcrumb.phase.store(phase, std::memory_order_release);
}

extern "C" void azahar_switch_dynarmic_jit_log_host_timing(
    const char* phase, std::uint32_t guest_pc, std::uint64_t ticks_to_run,
    std::uint64_t ticks_executed, std::uintptr_t run_entry,
    std::uint32_t run_entry_range_id) noexcept {
    if (host_timing_log_count >= 32) {
        return;
    }
    ++host_timing_log_count;
    Log("%s guest_pc=0x%08x ticks_to_run=%llu ticks_executed=%llu "
        "run_entry=0x%016llx run_entry_range_id=%u",
        phase != nullptr ? phase : "HostTiming", guest_pc,
        static_cast<unsigned long long>(ticks_to_run),
        static_cast<unsigned long long>(ticks_executed),
        static_cast<unsigned long long>(run_entry), run_entry_range_id);
}

extern "C" void azahar_switch_dynarmic_jit_update_breadcrumb(
    std::uint32_t phase, std::uintptr_t block_entry, std::uintptr_t callback_target,
    std::uintptr_t continuation, std::uintptr_t dispatcher_target, std::uint32_t guest_pc,
    std::uint32_t svc, std::uintptr_t host_lr, std::uintptr_t host_sp,
    std::uintptr_t host_x16, std::uintptr_t host_x17) noexcept {
    breadcrumb.block_entry = block_entry;
    breadcrumb.callback_target = callback_target;
    breadcrumb.continuation = continuation;
    breadcrumb.dispatcher_target = dispatcher_target != 0 ? dispatcher_target : last_dispatcher_target;
    breadcrumb.host_lr = host_lr;
    breadcrumb.host_sp = host_sp;
    breadcrumb.host_x16 = host_x16;
    breadcrumb.host_x17 = host_x17;
    breadcrumb.guest_pc = guest_pc;
    breadcrumb.svc = svc;
    breadcrumb.phase.store(phase, std::memory_order_release);
}

extern "C" void azahar_switch_dynarmic_jit_get_breadcrumb(
    JitExecutionBreadcrumb* out) noexcept {
    if (out != nullptr) {
        out->phase.store(breadcrumb.phase.load(std::memory_order_acquire),
                         std::memory_order_relaxed);
        out->block_entry = breadcrumb.block_entry;
        out->callback_target = breadcrumb.callback_target;
        out->continuation = breadcrumb.continuation;
        out->dispatcher_target = breadcrumb.dispatcher_target;
        out->host_lr = breadcrumb.host_lr;
        out->host_sp = breadcrumb.host_sp;
        out->host_x16 = breadcrumb.host_x16;
        out->host_x17 = breadcrumb.host_x17;
        out->guest_pc = breadcrumb.guest_pc;
        out->svc = breadcrumb.svc;
    }
}
