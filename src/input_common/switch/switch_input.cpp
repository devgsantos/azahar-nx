// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "input_common/switch/switch_input.h"

#include <algorithm>
#include <tuple>

#include "common/logging/log.h"
#include "common/param_package.h"

namespace InputCommon::Switch {

namespace {

ControllerState g_state;

// Map from ParamPackage "code" to a stable button bit.
std::uint64_t ButtonCodeToMask(const std::string& code) {
    using Button = InputCommon::Switch::Button;
    if (code == "a") {
        return static_cast<std::uint64_t>(Button::A);
    }
    if (code == "b") {
        return static_cast<std::uint64_t>(Button::B);
    }
    if (code == "x") {
        return static_cast<std::uint64_t>(Button::X);
    }
    if (code == "y") {
        return static_cast<std::uint64_t>(Button::Y);
    }
    if (code == "l") {
        return static_cast<std::uint64_t>(Button::L);
    }
    if (code == "r") {
        return static_cast<std::uint64_t>(Button::R);
    }
    if (code == "zl") {
        return static_cast<std::uint64_t>(Button::ZL);
    }
    if (code == "zr") {
        return static_cast<std::uint64_t>(Button::ZR);
    }
    if (code == "plus") {
        return static_cast<std::uint64_t>(Button::Plus);
    }
    if (code == "minus") {
        return static_cast<std::uint64_t>(Button::Minus);
    }
    if (code == "up") {
        return static_cast<std::uint64_t>(Button::DpadUp);
    }
    if (code == "down") {
        return static_cast<std::uint64_t>(Button::DpadDown);
    }
    if (code == "left") {
        return static_cast<std::uint64_t>(Button::DpadLeft);
    }
    if (code == "right") {
        return static_cast<std::uint64_t>(Button::DpadRight);
    }
    if (code == "stick_left") {
        return static_cast<std::uint64_t>(Button::StickLeft);
    }
    if (code == "stick_right") {
        return static_cast<std::uint64_t>(Button::StickRight);
    }
    if (code == "stick_up") {
        return static_cast<std::uint64_t>(Button::StickUp);
    }
    if (code == "stick_down") {
        return static_cast<std::uint64_t>(Button::StickDown);
    }
    return 0; // No bit -> always false.
}

class ButtonDevice final : public Input::ButtonDevice {
public:
    explicit ButtonDevice(std::uint64_t mask) : button_mask(mask) {}

    bool GetStatus() const override {
        return (g_state.buttons.load(std::memory_order_relaxed) & button_mask) != 0;
    }

private:
    std::uint64_t button_mask;
};

class AnalogDevice final : public Input::AnalogDevice {
public:
    explicit AnalogDevice(bool right_stick) : right_stick(right_stick) {}

    std::tuple<float, float> GetStatus() const override {
        const auto& state = g_state;
        const std::int32_t x = right_stick ? state.right_stick_x.load(std::memory_order_relaxed)
                                           : state.left_stick_x.load(std::memory_order_relaxed);
        const std::int32_t y = right_stick ? state.right_stick_y.load(std::memory_order_relaxed)
                                           : state.left_stick_y.load(std::memory_order_relaxed);
        // libnx analog stick range is roughly INT16_MIN..INT16_MAX.  Clamp and normalize.
        constexpr float scale = 1.0f / 32767.0f;
        const float nx = std::clamp(static_cast<float>(x) * scale, -1.0f, 1.0f);
        const float ny = std::clamp(static_cast<float>(y) * scale, -1.0f, 1.0f);
        return {nx, ny};
    }

private:
    bool right_stick;
};

class TouchDevice final : public Input::TouchDevice {
public:
    std::tuple<float, float, bool> GetStatus() const override {
        const bool pressed = g_state.touch_pressed.load(std::memory_order_relaxed);
        if (!pressed) {
            return {0.0f, 0.0f, false};
        }
        // libnx touch coordinates are 0..1280x720.  Normalize to 0..1.
        constexpr float width = 1280.0f;
        constexpr float height = 720.0f;
        const float x = std::clamp(static_cast<float>(g_state.touch_x.load(std::memory_order_relaxed)) / width,
                                     0.0f, 1.0f);
        const float y = std::clamp(static_cast<float>(g_state.touch_y.load(std::memory_order_relaxed)) / height,
                                     0.0f, 1.0f);
        return {x, y, true};
    }
};

} // namespace

ControllerState& GetControllerState() {
    return g_state;
}

void UpdateControllerState(std::uint64_t buttons, std::int32_t left_x, std::int32_t left_y,
                          std::int32_t right_x, std::int32_t right_y, std::int32_t touch_x,
                          std::int32_t touch_y, bool touch_pressed) {
    g_state.buttons.store(buttons, std::memory_order_relaxed);
    g_state.left_stick_x.store(left_x, std::memory_order_relaxed);
    g_state.left_stick_y.store(left_y, std::memory_order_relaxed);
    g_state.right_stick_x.store(right_x, std::memory_order_relaxed);
    g_state.right_stick_y.store(right_y, std::memory_order_relaxed);
    g_state.touch_x.store(touch_x, std::memory_order_relaxed);
    g_state.touch_y.store(touch_y, std::memory_order_relaxed);
    g_state.touch_pressed.store(touch_pressed, std::memory_order_relaxed);
}

std::unique_ptr<Input::ButtonDevice> ButtonFactory::Create(const Common::ParamPackage& params) {
    const std::string code = params.Get("code", "");
    const std::uint64_t mask = static_cast<std::uint64_t>(1) << ButtonCodeToMask(code);
    return std::make_unique<ButtonDevice>(mask);
}

std::unique_ptr<Input::AnalogDevice> AnalogFactory::Create(const Common::ParamPackage& params) {
    const bool right_stick = params.Get("stick", "left") == "right";
    return std::make_unique<AnalogDevice>(right_stick);
}

std::unique_ptr<Input::TouchDevice> TouchFactory::Create(const Common::ParamPackage& params) {
    (void)params;
    return std::make_unique<TouchDevice>();
}

} // namespace InputCommon::Switch
