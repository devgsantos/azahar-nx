// Copyright 2023 Citra Emulator Project
// Copyright 2024 Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#pragma once

#include <stdexcept>

#include "common/archives.h"
#include "common/microprofile.h"
#include "common/switch_trace.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/hle/service/gsp/gsp_gpu.h"
#include "core/hle/service/plgldr/plgldr.h"
#include "video_core/debug_utils/debug_utils.h"
#include "video_core/gpu.h"
#include "video_core/gpu_debugger.h"
#include "video_core/gpu_impl.h"
#include "video_core/pica/pica_core.h"
#include "video_core/pica/regs_lcd.h"
#include "video_core/renderer_base.h"
#include "video_core/renderer_software/sw_blitter.h"
#include "video_core/right_eye_disabler.h"
#include "video_core/video_core.h"

namespace VideoCore {
struct GPU::Impl {
    Core::Timing& timing;
    Core::System& system;
    Memory::MemorySystem& memory;
    std::shared_ptr<Pica::DebugContext> debug_context;
    Pica::PicaCore pica;
    GraphicsDebugger gpu_debugger;
    std::unique_ptr<RendererBase> renderer;
    RasterizerInterface* rasterizer;
    std::unique_ptr<SwRenderer::SwBlitter> sw_blitter;
    Core::TimingEventType* vblank_event;
    Service::GSP::InterruptHandler signal_interrupt;

    explicit Impl(Core::System& system, Frontend::EmuWindow& emu_window,
                  Frontend::EmuWindow* secondary_window)
        : timing{system.CoreTiming()}, system{system}, memory{system.Memory()},
          debug_context{Pica::g_debug_context}, pica{memory, debug_context} {
        SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl", "VideoCore creation");
        SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl.Renderer", "enter");
        renderer = VideoCore::CreateRenderer(emu_window, secondary_window, pica, system);
        if (!renderer) {
            SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl.Renderer", "failed_null_renderer");
            throw std::runtime_error("Deko3D renderer initialization failed");
        }
        SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl.Renderer", "VideoCore renderer created");

        SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl.Rasterizer", "enter");
        rasterizer = renderer->Rasterizer();
        if (!rasterizer) {
            SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl.Rasterizer", "failed_null_rasterizer");
            throw std::runtime_error("Deko3D renderer initialization failed");
        }
        SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl.Rasterizer", "Rasterizer creation");
        SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl.Rasterizer", "leave");

        SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl.Blitter", "enter");
        sw_blitter = std::make_unique<SwRenderer::SwBlitter>(memory, rasterizer);
        SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl.Blitter", "leave");
        SWITCH_TRACE_EVENT("VideoCore.GPU", "Impl", "leave");
    }
    ~Impl() = default;
};
} // namespace VideoCore
