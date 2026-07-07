// Copyright 2018 Citra Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <algorithm>
#include "common/archives.h"
#include "core/file_sys/delay_generator.h"

SERIALIZE_EXPORT_IMPL(FileSys::DefaultDelayGenerator)

namespace FileSys {

DelayGenerator::~DelayGenerator() = default;

u64 DefaultDelayGenerator::GetReadDelayNs(std::size_t length) {
#ifdef __SWITCH__
    // On real Switch hardware these host-side emulation delays are too expensive.
    // Return zero to avoid throttling file IO during boot/loading.
    return 0;
#else
    // This is the delay measured for a romfs read.
    // For now we will take that as a default
    static constexpr u64 slope(94);
    static constexpr u64 offset(582778);
    static constexpr u64 minimum(663124);
    u64 IPCDelayNanoseconds = std::max<u64>(static_cast<u64>(length) * slope + offset, minimum);
    return IPCDelayNanoseconds;
#endif
}

u64 DefaultDelayGenerator::GetOpenDelayNs() {
#ifdef __SWITCH__
    // Avoid expensive per-open sleep in Switch builds.
    return 0;
#else
    // This is the delay measured for a romfs open.
    // For now we will take that as a default
    static constexpr u64 IPCDelayNanoseconds(9438006);
    return IPCDelayNanoseconds;
#endif
}

} // namespace FileSys
