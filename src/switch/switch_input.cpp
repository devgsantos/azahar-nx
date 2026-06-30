// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_input.h"

#include "common/logging/log.h"
#include "switch_libnx.h"

namespace Azahar::Switch {
namespace {

} // namespace

bool InitializeInput() {
    LibNx::InputInitialize();
    LOG_INFO(Input, "Switch HID initialized with fixed Joy-Con mapping");
    return true;
}

InputState PollInput() {
    const LibNx::NativeInputState native = LibNx::PollInput();
    return {
        native.a,          native.b,           native.x,          native.y,
        native.l,          native.r,           native.zl,         native.zr,
        native.plus,       native.minus,       native.up,         native.down,
        native.left,       native.right,       native.stick_up,   native.stick_down,
        native.stick_left, native.stick_right,
    };
}

InputState NewlyPressed(const InputState& previous, const InputState& current) {
    return {
        current.a && !previous.a,
        current.b && !previous.b,
        current.x && !previous.x,
        current.y && !previous.y,
        current.l && !previous.l,
        current.r && !previous.r,
        current.zl && !previous.zl,
        current.zr && !previous.zr,
        current.plus && !previous.plus,
        current.minus && !previous.minus,
        current.up && !previous.up,
        current.down && !previous.down,
        current.left && !previous.left,
        current.right && !previous.right,
        current.stick_up && !previous.stick_up,
        current.stick_down && !previous.stick_down,
        current.stick_left && !previous.stick_left,
        current.stick_right && !previous.stick_right,
    };
}

void WaitForVBlank() {
    LibNx::ConsoleUpdate();
}

} // namespace Azahar::Switch
