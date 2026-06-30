// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

namespace Azahar::Switch {

struct InputState {
    bool a{};
    bool b{};
    bool x{};
    bool y{};
    bool l{};
    bool r{};
    bool zl{};
    bool zr{};
    bool plus{};
    bool minus{};
    bool up{};
    bool down{};
    bool left{};
    bool right{};
    bool stick_up{};
    bool stick_down{};
    bool stick_left{};
    bool stick_right{};
};

bool InitializeInput();
InputState PollInput();
InputState NewlyPressed(const InputState& previous, const InputState& current);
void WaitForVBlank();

} // namespace Azahar::Switch
