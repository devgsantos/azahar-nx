// SPDX-License-Identifier: GPL-2.0-or-later
// Lightweight Dynarmic hooks for stable Switch performance builds.

#include <cstdint>

#if !defined(AZAHAR_SWITCH_PERF_DIAGNOSTICS)

extern "C" void azahar_switch_dynarmic_jit_set_breadcrumb_phase(
    std::uint32_t phase, std::uintptr_t block_entry, std::uint32_t guest_pc) noexcept {
    (void)phase;
    (void)block_entry;
    (void)guest_pc;
}

extern "C" void azahar_switch_dynarmic_jit_update_breadcrumb(
    std::uint32_t phase, std::uintptr_t block_entry, std::uintptr_t callback_target,
    std::uintptr_t continuation, std::uintptr_t dispatcher_target, std::uint32_t guest_pc,
    std::uint32_t svc, std::uintptr_t host_lr, std::uintptr_t host_sp,
    std::uintptr_t host_x16, std::uintptr_t host_x17) noexcept {
    (void)phase;
    (void)block_entry;
    (void)callback_target;
    (void)continuation;
    (void)dispatcher_target;
    (void)guest_pc;
    (void)svc;
    (void)host_lr;
    (void)host_sp;
    (void)host_x16;
    (void)host_x17;
}

extern "C" void azahar_switch_dynarmic_jit_log_host_timing(
    const char* phase, std::uint32_t guest_pc, std::uint64_t ticks_to_run,
    std::uint64_t ticks_executed, std::uintptr_t run_entry,
    std::uint32_t run_entry_range_id) noexcept {
    (void)phase;
    (void)guest_pc;
    (void)ticks_to_run;
    (void)ticks_executed;
    (void)run_entry;
    (void)run_entry_range_id;
}

extern "C" void azahar_switch_dynarmic_jit_log_a32_svc(
    std::uint32_t svc, std::uintptr_t callback_target, std::uintptr_t continuation,
    std::uint32_t guest_pc) noexcept {
    (void)svc;
    (void)callback_target;
    (void)continuation;
    (void)guest_pc;
}

#endif
