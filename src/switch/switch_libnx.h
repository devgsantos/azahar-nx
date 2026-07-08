// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <cstdint>

namespace Azahar::Switch::LibNx {

struct NativeInputState {
    // Button bitfield using InputCommon::Switch::Button values.
    std::uint64_t buttons{};
    std::int32_t left_stick_x{};
    std::int32_t left_stick_y{};
    std::int32_t right_stick_x{};
    std::int32_t right_stick_y{};
    std::int32_t touch_x{};
    std::int32_t touch_y{};
    bool touch_pressed{};
};

void ConsoleInit();
void ConsoleExit();
void ConsoleUpdate();

void RomfsInit();
void RomfsExit();

bool AudioInitialize(std::uint32_t* result);
bool AudioStart(std::uint32_t* result);
void AudioShutdown();

void InputInitialize();
NativeInputState PollInput();

} // namespace Azahar::Switch::LibNx
