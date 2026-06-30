// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/renderer_deko3d.h"

#include <stdexcept>

#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "core/core.h"

namespace VideoCore::Deko3D {

RendererDeko3D::RendererDeko3D(Core::System& system_, Frontend::EmuWindow& window,
                               Frontend::EmuWindow* secondary_window)
    : RendererBase{system_, window, secondary_window}, presenter{state} {
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D", "enter");
    LOG_INFO(Render, "Renderer backend selected: Deko3D");
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D", "Renderer backend selected: Deko3D");
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D.State", "enter");
    if (!state.Initialize()) {
        SWITCH_TRACE_EVENTF("Deko3D", "RendererDeko3D.State", "failed", "error=%s",
                            state.LastError().c_str());
        throw std::runtime_error("Deko3D renderer initialization failed: " + state.LastError());
    }
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D.State", "leave");

    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D.TextureCache", "enter");
    if (!texture_cache.Initialize()) {
        SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D.TextureCache", "failed");
        throw std::runtime_error("Deko3D texture cache initialization failed");
    }
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D.TextureCache", "texture cache creation");
    LOG_INFO(Render, "Deko3D texture cache creation");

    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D.ShaderCache", "enter");
    if (!shader_cache.Initialize()) {
        SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D.ShaderCache", "failed");
        throw std::runtime_error("Deko3D shader cache initialization failed");
    }
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D.ShaderCache", "shader cache creation");
    LOG_INFO(Render, "Deko3D shader cache creation");

    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D.Rasterizer", "rasterizer creation");
    LOG_INFO(Render, "Deko3D rasterizer creation");
    initialized = true;
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D", "VideoCore renderer created");
    LOG_INFO(Render, "VideoCore renderer created");
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D", "leave");
}

RendererDeko3D::~RendererDeko3D() {
    state.WaitIdle();
}

void RendererDeko3D::SwapBuffers() {
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D::SwapBuffers", "enter");
    system.perf_stats->StartSwap();
    if (!presenter.PresentFrame()) {
        SWITCH_TRACE_EVENTF("Deko3D", "RendererDeko3D::SwapBuffers", "failed", "error=%s",
                            state.LastError().c_str());
        throw std::runtime_error("Deko3D frame presentation failed: " + state.LastError());
    }
    system.perf_stats->EndSwap();
    EndFrame();
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D::SwapBuffers", "leave");
}

void RendererDeko3D::TryPresent(int, bool) {
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D::TryPresent", "enter");
    if (!presenter.HasPresentedFrame()) {
        presenter.PresentFrame();
    }
    SWITCH_TRACE_EVENT("Deko3D", "RendererDeko3D::TryPresent", "leave");
}

} // namespace VideoCore::Deko3D
