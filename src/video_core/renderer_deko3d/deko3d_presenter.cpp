// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_presenter.h"

#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "video_core/renderer_deko3d/deko3d_state.h"

namespace VideoCore::Deko3D {

Presenter::Presenter(State& state_) : state{state_} {}

bool Presenter::PresentFrame() {
    SWITCH_TRACE_EVENT("Deko3D", "Presenter::PresentFrame", "enter");
    const bool presented = state.PresentClearFrame(0.02f, 0.04f, 0.06f, 1.0f);
    if (presented && !presented_frame) {
        LOG_INFO(Render, "Deko3D first frame presented");
        SWITCH_TRACE_EVENT("Deko3D", "Presenter::PresentFrame", "first frame presented");
        presented_frame = true;
    }
    SWITCH_TRACE_EVENTF("Deko3D", "Presenter::PresentFrame", "leave", "presented=%s",
                        presented ? "true" : "false");
    return presented;
}

} // namespace VideoCore::Deko3D
