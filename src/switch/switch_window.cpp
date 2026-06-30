// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_window.h"

#include "common/logging/log.h"
#include "core/frontend/framebuffer_layout.h"
#include "switch_debug_log.h"

namespace Azahar::Switch {

SwitchWindow::SwitchWindow() {
    SWITCH_EARLY_LOG("SwitchWindow constructor begin");
    WindowConfig config;
    config.fullscreen = true;
    config.res_width = 1280;
    config.res_height = 720;
    config.min_client_area_size = {1280, 720};
    SetConfig(config);
    ProcessConfigurationChanges();

    window_info.type = Frontend::WindowSystemType::Switch;
    UpdateCurrentFramebufferLayout(1280, 720);
    SWITCH_EARLY_LOG("SwitchWindow OnFramebufferLayoutChanged called width=1280 height=720");
    initialized = true;
    LOG_INFO(Frontend, "Switch emu window initialized at 1280x720");
    SWITCH_EARLY_LOG("SwitchWindow constructor end initialized=true");
}

void SwitchWindow::PollEvents() {
    TraceMethod("PollEvents called");
    ProcessConfigurationChanges();
}

void SwitchWindow::MakeCurrent() {
    TraceMethod("MakeCurrent called");
}

void SwitchWindow::DoneCurrent() {
    TraceMethod("DoneCurrent called");
}

void SwitchWindow::SwapBuffers() {
    TraceMethod("SwapBuffers called");
    clear_next_frame = false;
}

void SwitchWindow::SetupFramebuffer() {
    TraceMethod("SetupFramebuffer called");
}

bool SwitchWindow::NeedsClearing() const {
    return clear_next_frame;
}

bool SwitchWindow::IsValid() const {
    const auto& layout = GetFramebufferLayout();
    return initialized && GetWindowInfo().type == Frontend::WindowSystemType::Switch &&
           layout.width > 0 && layout.height > 0;
}

void SwitchWindow::TraceMethod(const char* method) const {
    if (trace_lines >= 80) {
        return;
    }
    ++trace_lines;
    const auto& layout = GetFramebufferLayout();
    SWITCH_EARLY_LOGF("SwitchWindow %s layout=%ux%u initialized=%s", method, layout.width,
                      layout.height, initialized ? "true" : "false");
}

void SwitchWindow::OnMinimalClientAreaChangeRequest(std::pair<u32, u32> minimal_size) {
    SWITCH_EARLY_LOGF("SwitchWindow OnMinimalClientAreaChangeRequest called width=%u height=%u",
                      minimal_size.first, minimal_size.second);
}

} // namespace Azahar::Switch
