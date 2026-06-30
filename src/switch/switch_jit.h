// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

namespace Azahar::Switch {

// Verifies that Horizon/libnx can create a writable/executable JIT buffer,
// execute code through the RX alias, and return the expected value.
bool RunJitSelfTest();

} // namespace Azahar::Switch
