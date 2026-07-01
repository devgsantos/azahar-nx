/* This file is part of the dynarmic project.
 * Copyright (c) 2022 MerryMage
 * SPDX-License-Identifier: 0BSD
 */

#pragma once

#if defined(__SWITCH__)
#include <cassert>
#include <cstdint>
#endif

#include "dynarmic/backend/arm64/a32_address_space.h"
#include "dynarmic/backend/arm64/a32_jitstate.h"

#if defined(__SWITCH__)
extern "C" bool azahar_switch_dynarmic_jit_is_rx_address(
    std::uintptr_t address) noexcept;
extern "C" void azahar_switch_dynarmic_jit_log_run_entry(
    std::uintptr_t run_entry) noexcept;
extern "C" void azahar_switch_dynarmic_jit_set_breadcrumb_phase(
    std::uint32_t phase, std::uintptr_t block_entry, std::uint32_t guest_pc) noexcept;
#endif

namespace Dynarmic::Backend::Arm64 {

class A32Core final {
public:
    explicit A32Core(const A32::UserConfig&) {}

    HaltReason Run(A32AddressSpace& process, A32JitState& thread_ctx, volatile u32* halt_reason) {
        const auto location_descriptor = thread_ctx.GetLocationDescriptor();
        const auto entry_point = process.GetOrEmit(location_descriptor);
#if defined(__SWITCH__)
        const auto run_entry = reinterpret_cast<std::uintptr_t>(entry_point);
        azahar_switch_dynarmic_jit_log_run_entry(run_entry);
        assert(azahar_switch_dynarmic_jit_is_rx_address(run_entry));
        azahar_switch_dynarmic_jit_set_breadcrumb_phase(
            6, run_entry, A32::LocationDescriptor{location_descriptor}.PC());
        const HaltReason result = process.prelude_info.run_code(entry_point, &thread_ctx, halt_reason);
        azahar_switch_dynarmic_jit_set_breadcrumb_phase(
            7, run_entry, A32::LocationDescriptor{thread_ctx.GetLocationDescriptor()}.PC());
        return result;
#else
#if defined(__SWITCH__)
        // AZAHAR_SWITCH_HOST_TICK_BUDGET_V6
        // Resolve the virtual timing callback in C++ and pass its result in X3.
        // Generated code no longer tail-branches through X16 merely to obtain
        // the initial cycle budget.
        const u64 ticks_to_run = process.conf.enable_cycle_counting
                                     ? process.conf.callbacks->GetTicksRemaining()
                                     : 0;
        return process.prelude_info.run_code(entry_point, &thread_ctx, halt_reason,
                                             ticks_to_run);
#else
        return process.prelude_info.run_code(entry_point, &thread_ctx, halt_reason);
#endif
#endif
    }

    HaltReason Step(A32AddressSpace& process, A32JitState& thread_ctx, volatile u32* halt_reason) {
        const auto location_descriptor = A32::LocationDescriptor{thread_ctx.GetLocationDescriptor()}.SetSingleStepping(true);
        const auto entry_point = process.GetOrEmit(location_descriptor);
#if defined(__SWITCH__)
        const auto run_entry = reinterpret_cast<std::uintptr_t>(entry_point);
        azahar_switch_dynarmic_jit_log_run_entry(run_entry);
        assert(azahar_switch_dynarmic_jit_is_rx_address(run_entry));
        azahar_switch_dynarmic_jit_set_breadcrumb_phase(6, run_entry, location_descriptor.PC());
        const HaltReason result = process.prelude_info.step_code(entry_point, &thread_ctx, halt_reason);
        azahar_switch_dynarmic_jit_set_breadcrumb_phase(
            7, run_entry, A32::LocationDescriptor{thread_ctx.GetLocationDescriptor()}.PC());
        return result;
#else
#if defined(__SWITCH__)
        // The generated single-step prelude retains its fixed one-cycle budget;
        // the fourth argument only keeps the Switch function signature ABI-correct.
        return process.prelude_info.step_code(entry_point, &thread_ctx, halt_reason, 1);
#else
        return process.prelude_info.step_code(entry_point, &thread_ctx, halt_reason);
#endif
#endif
    }
};

}  // namespace Dynarmic::Backend::Arm64
