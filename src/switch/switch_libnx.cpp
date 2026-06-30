// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_libnx.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

namespace Azahar::Switch::LibNx {
namespace {

#ifdef __SWITCH__
PadState pad;

bool HasStickDirection(const HidAnalogStickState& stick, int x, int y) {
    constexpr s32 threshold = 12000;
    return (x < 0 && stick.x < -threshold) || (x > 0 && stick.x > threshold) ||
           (y < 0 && stick.y < -threshold) || (y > 0 && stick.y > threshold);
}
#endif

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

    state.a = (buttons & HidNpadButton_A) != 0;
    state.b = (buttons & HidNpadButton_B) != 0;
    state.x = (buttons & HidNpadButton_X) != 0;
    state.y = (buttons & HidNpadButton_Y) != 0;
    state.l = (buttons & HidNpadButton_L) != 0;
    state.r = (buttons & HidNpadButton_R) != 0;
    state.zl = (buttons & HidNpadButton_ZL) != 0;
    state.zr = (buttons & HidNpadButton_ZR) != 0;
    state.plus = (buttons & HidNpadButton_Plus) != 0;
    state.minus = (buttons & HidNpadButton_Minus) != 0;
    state.up = (buttons & HidNpadButton_Up) != 0;
    state.down = (buttons & HidNpadButton_Down) != 0;
    state.left = (buttons & HidNpadButton_Left) != 0;
    state.right = (buttons & HidNpadButton_Right) != 0;
    state.stick_up = HasStickDirection(left, 0, 1) || HasStickDirection(right, 0, 1);
    state.stick_down = HasStickDirection(left, 0, -1) || HasStickDirection(right, 0, -1);
    state.stick_left = HasStickDirection(left, -1, 0) || HasStickDirection(right, -1, 0);
    state.stick_right = HasStickDirection(left, 1, 0) || HasStickDirection(right, 1, 0);
#endif
    return state;
}

} // namespace Azahar::Switch::LibNx
