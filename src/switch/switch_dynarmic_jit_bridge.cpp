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
thread_local JitExecutionBreadcrumb breadcrumb{};
std::uint32_t next_jit_id = 1;
thread_local std::uintptr_t last_dispatcher_target = 0;
thread_local std::uintptr_t last_run_entry = 0;
thread_local std::uint32_t host_timing_log_count = 0;
thread_local std::uint32_t a32_svc_log_count = 0;
#if defined(AZAHAR_SWITCH_DYNARMIC_VERBOSE_TEXT_LOGS)
thread_local std::uint32_t run_entry_log_count = 0;
#endif
std::atomic<std::uint64_t> partial_publish_count{0};
std::atomic<std::uint64_t> full_publish_count{0};
std::atomic<std::uint64_t> bytes_flushed{0};
std::atomic<std::uint64_t> bytes_invalidated{0};
std::atomic<std::uint64_t> cache_clear_count{0};
std::atomic<std::uint64_t> compiled_block_count{0};

void LogTaggedV(const char* tag, const char* format, va_list args) noexcept {
    std::FILE* file = std::fopen(LogPath, "a");
    va_list file_args;
    va_copy(file_args, args);

    std::fprintf(stderr, "[%s] ", tag != nullptr ? tag : "Dynarmic.JIT");
    std::vfprintf(stderr, format, args);
    std::fprintf(stderr, "\n");
    std::fflush(stderr);

    if (file != nullptr) {
        std::fprintf(file, "[%s] ", tag != nullptr ? tag : "Dynarmic.JIT");
        std::vfprintf(file, format, file_args);
        std::fputc('\n', file);
        std::fflush(file);
        std::fclose(file);
    }
    va_end(file_args);
}

void LogTaggedMessage(const char* tag, const char* message) noexcept {
    std::FILE* file = std::fopen(LogPath, "a");

    std::fprintf(stderr, "[%s] %s\n", tag != nullptr ? tag : "Dynarmic.JIT",
                 message != nullptr ? message : "");
    std::fflush(stderr);

    if (file != nullptr) {
        std::fprintf(file, "[%s] %s\n", tag != nullptr ? tag : "Dynarmic.JIT",
                     message != nullptr ? message : "");
        std::fflush(file);
        std::fclose(file);
    }
}

void Log(const char* format, ...) noexcept {
    va_list args;
    va_start(args, format);
    LogTaggedV("Dynarmic.JIT", format, args);
    va_end(args);
}

void LogLine(const char* tag, const char* message) noexcept {
    LogTaggedMessage(tag, message);
}

bool IsErrorLikeMessage(const char* message) noexcept {
    if (message == nullptr) {
        return false;
    }

    return std::strstr(message, "error") != nullptr ||
           std::strstr(message, "Error") != nullptr ||
           std::strstr(message, "failed") != nullptr ||
           std::strstr(message, "Failed") != nullptr ||
           std::strstr(message, "invalid") != nullptr ||
           std::strstr(message, "Invalid") != nullptr ||
           std::strstr(message, "abort") != nullptr ||
           std::strstr(message, "Abort") != nullptr ||
           std::strstr(message, "exception") != nullptr ||
           std::strstr(message, "Exception") != nullptr ||
           std::strstr(message, "unmapped") != nullptr ||
           std::strstr(message, "Unmapped") != nullptr;
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

void PublishPartialRange(SwitchDynarmicJit& handle, std::size_t offset,
                         std::size_t size) noexcept {
    if (size == 0) {
        return;
    }

    auto* rw = static_cast<std::uint8_t*>(jitGetRwAddr(&handle.jit));
    auto* rx = static_cast<std::uint8_t*>(jitGetRxAddr(&handle.jit));
    armDCacheFlush(rw + offset, size);
    __asm__ volatile("dsb ish" ::: "memory");
    armICacheInvalidate(rx + offset, size);
    __asm__ volatile("dsb ish\nisb" ::: "memory");

    handle.jit.is_executable = true;
    partial_publish_count.fetch_add(1, std::memory_order_relaxed);
    bytes_flushed.fetch_add(size, std::memory_order_relaxed);
    bytes_invalidated.fetch_add(size, std::memory_order_relaxed);
}

bool PublishFullRange(SwitchDynarmicJit& handle, std::size_t offset,
                      std::size_t size) noexcept {
    auto* rw = static_cast<std::uint8_t*>(jitGetRwAddr(&handle.jit));
    auto* rx = static_cast<std::uint8_t*>(jitGetRxAddr(&handle.jit));
    if (size != 0) {
        armDCacheFlush(rw + offset, size);
        __asm__ volatile("dsb ish" ::: "memory");
    }

    const Result rc = jitTransitionToExecutable(&handle.jit);
    if (R_FAILED(rc)) {
        Log("jitTransitionToExecutable failed rc=0x%08x offset=%zu size=%zu",
            rc, offset, size);
        return false;
    }

    if (size != 0) {
        armICacheInvalidate(rx + offset, size);
        __asm__ volatile("dsb ish\nisb" ::: "memory");
    }

    full_publish_count.fetch_add(1, std::memory_order_relaxed);
    bytes_flushed.fetch_add(size, std::memory_order_relaxed);
    bytes_invalidated.fetch_add(size, std::memory_order_relaxed);
    return true;
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

    const bool complete_allocation = offset == 0 && size == total_size;

#if defined(AZAHAR_SWITCH_JIT_PARTIAL_PUBLISH)
    if (handle->jit.type == JitType_CodeMemory && !complete_allocation) {
        PublishPartialRange(*handle, offset, size);
        return true;
    }
#endif

    return PublishFullRange(*handle, offset, size);
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
#if defined(AZAHAR_SWITCH_DYNARMIC_VERBOSE_TEXT_LOGS)
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
#else
    (void)name;
    (void)address;
#endif
}

extern "C" void azahar_switch_dynarmic_jit_log_run_entry(
    std::uintptr_t run_entry) noexcept {
    last_run_entry = run_entry;
#if defined(AZAHAR_SWITCH_DYNARMIC_VERBOSE_TEXT_LOGS)
    if (run_entry_log_count >= 8) {
        return;
    }
    ++run_entry_log_count;
    LogAddressFields("run_entry", run_entry);
#endif
}

extern "C" std::uintptr_t azahar_switch_dynarmic_jit_get_run_entry() noexcept {
    return last_run_entry;
}

extern "C" void azahar_switch_dynarmic_jit_log_message(
    const char* tag, const char* message) noexcept {
    if (tag != nullptr && std::strcmp(tag, "Dynarmic.Block") == 0 &&
        !IsErrorLikeMessage(message)) {
        return;
    }
    LogTaggedMessage(tag, message);
}

extern "C" void azahar_switch_dynarmic_jit_record_cache_clear() noexcept {
    cache_clear_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void azahar_switch_dynarmic_jit_record_block_compiled() noexcept {
    compiled_block_count.fetch_add(1, std::memory_order_relaxed);
}

extern "C" void azahar_switch_dynarmic_jit_take_publish_stats(
    std::uint64_t* partial_publishes, std::uint64_t* full_publishes,
    std::uint64_t* flushed, std::uint64_t* invalidated,
    std::uint64_t* cache_clears, std::uint64_t* blocks_compiled) noexcept {
    if (partial_publishes != nullptr) {
        *partial_publishes = partial_publish_count.exchange(0, std::memory_order_relaxed);
    }
    if (full_publishes != nullptr) {
        *full_publishes = full_publish_count.exchange(0, std::memory_order_relaxed);
    }
    if (flushed != nullptr) {
        *flushed = bytes_flushed.exchange(0, std::memory_order_relaxed);
    }
    if (invalidated != nullptr) {
        *invalidated = bytes_invalidated.exchange(0, std::memory_order_relaxed);
    }
    if (cache_clears != nullptr) {
        *cache_clears = cache_clear_count.exchange(0, std::memory_order_relaxed);
    }
    if (blocks_compiled != nullptr) {
        *blocks_compiled = compiled_block_count.exchange(0, std::memory_order_relaxed);
    }
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

extern "C" void azahar_switch_dynarmic_jit_log_a32_svc(
    std::uint32_t svc, std::uintptr_t callback_target, std::uintptr_t continuation,
    std::uint32_t guest_pc) noexcept {
    const JitAddressInfo continuation_info = ClassifyAddress(continuation);
    breadcrumb.block_entry = last_run_entry;
    breadcrumb.callback_target = callback_target;
    breadcrumb.continuation = continuation;
    breadcrumb.dispatcher_target = last_dispatcher_target;
    breadcrumb.host_lr = continuation;
    breadcrumb.host_sp = 0;
    breadcrumb.host_x16 = callback_target;
    breadcrumb.host_x17 = 0;
    breadcrumb.guest_pc = guest_pc;
    breadcrumb.svc = svc;
    breadcrumb.phase.store(static_cast<std::uint32_t>(JitExecutionPhase::BeforeCallback),
                           std::memory_order_release);

#if defined(AZAHAR_SWITCH_TRACE_DYNARMIC_SVC)
    if (a32_svc_log_count >= 32) {
        return;
    }
    ++a32_svc_log_count;
    Log("A32CallSVC before callback svc=0x%08x guest_pc=0x%08x "
        "callback_target=0x%016llx continuation=0x%016llx continuation_class=%s "
        "continuation_id=%u continuation_offset=0x%zx run_entry=0x%016llx",
        svc, guest_pc, static_cast<unsigned long long>(callback_target),
        static_cast<unsigned long long>(continuation),
        ClassName(continuation_info.address_class), continuation_info.id,
        continuation_info.offset, static_cast<unsigned long long>(last_run_entry));
#endif
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
