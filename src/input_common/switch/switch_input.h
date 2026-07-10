// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <atomic>
#include <cstdint>

#include "core/frontend/input.h"

namespace InputCommon::Switch {

// Stable button bit identifiers shared between the frontend and the input factory.
enum class Button : std::uint64_t {
    A = 1ULL << 0,
    B = 1ULL << 1,
    X = 1ULL << 2,
    Y = 1ULL << 3,
    L = 1ULL << 4,
    R = 1ULL << 5,
    ZL = 1ULL << 6,
    ZR = 1ULL << 7,
    Plus = 1ULL << 8,
    Minus = 1ULL << 9,
    DpadUp = 1ULL << 10,
    DpadDown = 1ULL << 11,
    DpadLeft = 1ULL << 12,
    DpadRight = 1ULL << 13,
    StickLeft = 1ULL << 14,
    StickRight = 1ULL << 15,
    StickUp = 1ULL << 16,
    StickDown = 1ULL << 17,
};

// Current controller state updated by the frontend each frame.
struct ControllerState {
    std::atomic<std::uint64_t> buttons{0};
    std::atomic<std::int32_t> left_stick_x{0};
    std::atomic<std::int32_t> left_stick_y{0};
    std::atomic<std::int32_t> right_stick_x{0};
    std::atomic<std::int32_t> right_stick_y{0};
    std::atomic<std::int32_t> touch_x{0};
    std::atomic<std::int32_t> touch_y{0};
    std::atomic<bool> touch_pressed{false};
};

ControllerState& GetControllerState();

// Called by the frontend after polling libnx HID.
void UpdateControllerState(std::uint64_t buttons, std::int32_t left_x, std::int32_t left_y,
                         std::int32_t right_x, std::int32_t right_y, std::int32_t touch_x,
                         std::int32_t touch_y, bool touch_pressed);

class ButtonFactory final : public Input::Factory<Input::ButtonDevice> {
public:
    std::unique_ptr<Input::ButtonDevice> Create(const Common::ParamPackage& params) override;
};

class AnalogFactory final : public Input::Factory<Input::AnalogDevice> {
public:
    std::unique_ptr<Input::AnalogDevice> Create(const Common::ParamPackage& params) override;
};

class TouchFactory final : public Input::Factory<Input::TouchDevice> {
public:
    std::unique_ptr<Input::TouchDevice> Create(const Common::ParamPackage& params) override;
};

} // namespace InputCommon::Switch
