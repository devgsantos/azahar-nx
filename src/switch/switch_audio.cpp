// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_audio.h"

#include "common/logging/log.h"
#include "switch_libnx.h"

namespace Azahar::Switch {

bool SwitchAudio::Initialize() {
    std::uint32_t result{};
    if (!LibNx::AudioInitialize(&result)) {
        LOG_ERROR(Audio, "audrenInitialize failed: 0x{:08X}; continuing without native audio",
                  result);
        return false;
    }
    std::uint32_t start_result{};
    if (!LibNx::AudioStart(&start_result)) {
        LOG_ERROR(Audio,
                  "audrenStartAudioRenderer failed: 0x{:08X}; continuing without native audio",
                  start_result);
        LibNx::AudioShutdown();
        return false;
    }
    initialized = true;
    LOG_INFO(Audio, "Switch audren audio initialized");
    return true;
}

void SwitchAudio::Shutdown() {
    if (!initialized) {
        return;
    }
    LibNx::AudioShutdown();
    initialized = false;
}

} // namespace Azahar::Switch
