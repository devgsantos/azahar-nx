// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_input.h"

#include "common/logging/log.h"
#include "input_common/switch/switch_input.h"
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
    InputCommon::Switch::UpdateControllerState(
        native.buttons, native.left_stick_x, native.left_stick_y, native.right_stick_x,
        native.right_stick_y, native.touch_x, native.touch_y, native.touch_pressed);
    return InputState{
        .buttons = native.buttons,
        .left_stick_x = native.left_stick_x,
        .left_stick_y = native.left_stick_y,
        .right_stick_x = native.right_stick_x,
        .right_stick_y = native.right_stick_y,
        .touch_x = native.touch_x,
        .touch_y = native.touch_y,
        .touch_pressed = native.touch_pressed,
    };
}

InputState NewlyPressed(const InputState& previous, const InputState& current) {
    return InputState{
        .buttons = current.buttons & ~previous.buttons,
        .left_stick_x = current.left_stick_x,
        .left_stick_y = current.left_stick_y,
        .right_stick_x = current.right_stick_x,
        .right_stick_y = current.right_stick_y,
        .touch_x = current.touch_x,
        .touch_y = current.touch_y,
        .touch_pressed = current.touch_pressed && !previous.touch_pressed,
    };
}

void WaitForVBlank() {
    LibNx::ConsoleUpdate();
}

} // namespace Azahar::Switch
