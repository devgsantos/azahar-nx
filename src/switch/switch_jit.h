// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <cstdint>

namespace Azahar::Switch {

struct JitRunStats {
    std::uint64_t calls = 0;
    std::uint64_t host_ns = 0;
    std::uint64_t requested_ticks = 0;
    std::uint64_t executed_ticks = 0;
    std::uint64_t zero_tick_calls = 0;
};

// Verifies that Horizon/libnx can create a writable/executable JIT buffer,
// execute code through the RX alias, and return the expected value.
bool RunJitSelfTest();
JitRunStats TakeJitRunStats();

} // namespace Azahar::Switch
