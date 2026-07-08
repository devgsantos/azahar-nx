// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_libnx.h"

#include "input_common/switch/switch_input.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace Azahar::Switch::LibNx {
namespace {

#ifdef __SWITCH__
PadState pad;
#endif

using SwitchButton = InputCommon::Switch::Button;

} // namespace

void ConsoleInit() {
#ifdef __SWITCH__
    consoleInit(nullptr);
#endif
}

void ConsoleExit() {
#ifdef __SWITCH__
    consoleExit(nullptr);
#endif
}

void ConsoleUpdate() {
#ifdef __SWITCH__
    consoleUpdate(nullptr);
#endif
}

void RomfsInit() {
#ifdef __SWITCH__
    romfsInit();
#endif
}

void RomfsExit() {
#ifdef __SWITCH__
    romfsExit();
#endif
}

bool AudioInitialize(std::uint32_t* result) {
#ifdef __SWITCH__
    const AudioRendererConfig config{
        AudioRendererOutputRate_48kHz,
        24,
        0,
        1,
        1,
        2,
    };
    const Result rc = audrenInitialize(&config);
    if (result) {
        *result = rc;
    }
    return R_SUCCEEDED(rc);
#else
    if (result) {
        *result = 0;
    }
    return true;
#endif
}

bool AudioStart(std::uint32_t* result) {
#ifdef __SWITCH__
    const Result rc = audrenStartAudioRenderer();
    if (result) {
        *result = rc;
    }
    return R_SUCCEEDED(rc);
#else
    if (result) {
        *result = 0;
    }
    return true;
#endif
}

void AudioShutdown() {
#ifdef __SWITCH__
    audrenStopAudioRenderer();
    audrenExit();
#endif
}

void InputInitialize() {
#ifdef __SWITCH__
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&pad);
#endif
}

NativeInputState PollInput() {
    NativeInputState state;
#ifdef __SWITCH__
    padUpdate(&pad);
    const u64 buttons = padGetButtons(&pad);
    const HidAnalogStickState left = padGetStickPos(&pad, 0);
    const HidAnalogStickState right = padGetStickPos(&pad, 1);

    std::uint64_t mask = 0;
    auto Add = [&](u64 hid, SwitchButton bit) {
        if ((buttons & hid) != 0) {
            mask |= static_cast<std::uint64_t>(bit);
        }
    };

    Add(HidNpadButton_A, SwitchButton::A);
    Add(HidNpadButton_B, SwitchButton::B);
    Add(HidNpadButton_X, SwitchButton::X);
    Add(HidNpadButton_Y, SwitchButton::Y);
    Add(HidNpadButton_L, SwitchButton::L);
    Add(HidNpadButton_R, SwitchButton::R);
    Add(HidNpadButton_ZL, SwitchButton::ZL);
    Add(HidNpadButton_ZR, SwitchButton::ZR);
    Add(HidNpadButton_Plus, SwitchButton::Plus);
    Add(HidNpadButton_Minus, SwitchButton::Minus);
    Add(HidNpadButton_Up, SwitchButton::DpadUp);
    Add(HidNpadButton_Down, SwitchButton::DpadDown);
    Add(HidNpadButton_Left, SwitchButton::DpadLeft);
    Add(HidNpadButton_Right, SwitchButton::DpadRight);

    // Map left/right stick thresholds to digital stick directions so the same button mapping
    // can be used by games that expect the dpad/analog-as-button path.
    constexpr s32 threshold = 12000;
    auto AddStick = [&](s32 x, s32 y, SwitchButton left_bit, SwitchButton right_bit,
                        SwitchButton up_bit, SwitchButton down_bit) {
        if (x < -threshold) {
            mask |= static_cast<std::uint64_t>(left_bit);
        } else if (x > threshold) {
            mask |= static_cast<std::uint64_t>(right_bit);
        }
        if (y > threshold) {
            mask |= static_cast<std::uint64_t>(up_bit);
        } else if (y < -threshold) {
            mask |= static_cast<std::uint64_t>(down_bit);
        }
    };
    AddStick(left.x, left.y, SwitchButton::StickLeft, SwitchButton::StickRight,
             SwitchButton::StickUp, SwitchButton::StickDown);
    AddStick(right.x, right.y, SwitchButton::StickLeft, SwitchButton::StickRight,
             SwitchButton::StickUp, SwitchButton::StickDown);

    state.buttons = mask;
    state.left_stick_x = left.x;
    state.left_stick_y = left.y;
    state.right_stick_x = right.x;
    state.right_stick_y = right.y;

    HidTouchScreenState touch{};
    if (R_SUCCEEDED(hidGetTouchScreenStates(&touch, 1)) && touch.count > 0) {
        const auto& first = touch.touches[0];
        state.touch_x = first.x;
        state.touch_y = first.y;
        state.touch_pressed = true;
    } else {
        state.touch_pressed = false;
    }
#endif
    return state;
}

} // namespace Azahar::Switch::LibNx
