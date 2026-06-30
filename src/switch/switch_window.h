// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include "core/frontend/emu_window.h"

namespace Azahar::Switch {

class SwitchWindow final : public Frontend::EmuWindow {
public:
    SwitchWindow();
    ~SwitchWindow() override = default;

    void PollEvents() override;
    void MakeCurrent() override;
    void DoneCurrent() override;
    void SwapBuffers() override;
    void SetupFramebuffer() override;
    bool NeedsClearing() const override;
    bool IsValid() const;

private:
    void TraceMethod(const char* method) const;
    void OnMinimalClientAreaChangeRequest(std::pair<u32, u32> minimal_size) override;

    bool clear_next_frame = true;
    bool initialized = false;
    mutable int trace_lines = 0;
};

} // namespace Azahar::Switch
