// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_presenter.h"

#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/gpu.h"
#include "video_core/pica/pica_core.h"

namespace VideoCore::Deko3D {

Presenter::Presenter(State& state_, Core::System& system_) : state{state_}, system{system_} {}

bool Presenter::PresentFrame() {
    SWITCH_TRACE_EVENT("Deko3D", "Presenter::PresentFrame", "enter");

    try {
        auto& pica_core = system.GPU().PicaCore();
        auto& memory = system.Memory();
        const auto& framebuffer_config = pica_core.regs.framebuffer_config;

        // Get active framebuffer addresses for top and bottom screens
        PAddr fb0_addr = framebuffer_config[0].active_fb == 0 ? framebuffer_config[0].address_left1
                                                              : framebuffer_config[0].address_left2;
        PAddr fb1_addr = framebuffer_config[1].active_fb == 0 ? framebuffer_config[1].address_left1
                                                              : framebuffer_config[1].address_left2;

        SWITCH_TRACE_EVENTF("Deko3D", "Presenter::PresentFrame", "framebuffer addresses",
                            "fb0_addr=0x%08x fb1_addr=0x%08x", fb0_addr, fb1_addr);

        // Copy top screen (400x240 RGB565 or RGBA8)
        {
            const auto& fb0 = framebuffer_config[0];
            const u32 width = fb0.width.Value();
            const u32 height = fb0.height.Value();
            const u32 stride = fb0.stride;

            u8* fb0_ptr = memory.GetPhysicalPointer(fb0_addr);
            if (fb0_ptr && width > 0 && height > 0 && stride > 0) {
                // For now, assume RGBA8 format (420 KB for 400x240)
                // TODO: Handle RGB565 and other formats with conversion
                const u32 copy_size = stride * height;
                if (copy_size <= 400 * 240 * 4) {
                    memcpy(state.GetScreenDataBuffer(), fb0_ptr, copy_size);
                    SWITCH_TRACE_EVENTF("Deko3D", "Presenter::PresentFrame", "top screen copied",
                                        "width=%u height=%u stride=%u size=%u",
                                        width, height, stride, copy_size);
                }
            }
        }

        // Copy bottom screen (320x240 RGB565 or RGBA8)
        {
            const auto& fb1 = framebuffer_config[1];
            const u32 width = fb1.width.Value();
            const u32 height = fb1.height.Value();
            const u32 stride = fb1.stride;

            u8* fb1_ptr = memory.GetPhysicalPointer(fb1_addr);
            if (fb1_ptr && width > 0 && height > 0 && stride > 0) {
                // Copy to offset in buffer (after top screen data)
                const u32 top_size = 400 * 240 * 4;
                const u32 copy_size = stride * height;
                if ((top_size + copy_size) <= state.GetScreenDataSize()) {
                    u8* buffer = static_cast<u8*>(state.GetScreenDataBuffer()) + top_size;
                    memcpy(buffer, fb1_ptr, copy_size);
                    SWITCH_TRACE_EVENTF("Deko3D", "Presenter::PresentFrame", "bottom screen copied",
                                        "width=%u height=%u stride=%u size=%u",
                                        width, height, stride, copy_size);
                }
            }
        }

    } catch (const std::exception& e) {
        LOG_WARNING(Render, "Deko3D Presenter framebuffer access error: {}", e.what());
    }

    const bool presented = state.PresentScreenTexturesFrame();
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
