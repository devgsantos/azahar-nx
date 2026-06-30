// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <cstdint>

namespace Azahar::Switch::LibNx {

struct NativeInputState {
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
