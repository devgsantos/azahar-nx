// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include "input_common/switch/switch_input.h"

namespace Azahar::Switch {

struct InputState {
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

inline bool IsButtonPressed(const InputState& input, InputCommon::Switch::Button button) {
    return (input.buttons & static_cast<std::uint64_t>(button)) != 0;
}

bool InitializeInput();
InputState PollInput();
InputState NewlyPressed(const InputState& previous, const InputState& current);
void WaitForVBlank();

} // namespace Azahar::Switch
