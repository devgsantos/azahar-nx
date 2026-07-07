// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_presenter.h"

#include <algorithm>
#include <cstring>

#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/gpu.h"
#include "video_core/pica/pica_core.h"

namespace VideoCore::Deko3D {
namespace {

u32 EstimateBytesPerPixel(u32 width, u32 stride) {
    if (width == 0) {
        return 0;
    }
    const u32 bpp = stride / width;
    if (bpp == 2 || bpp == 3 || bpp == 4) {
        return bpp;
    }
    return 4;
}

void DecodePixelToRgba(const u8* src, u32 bpp, u8* dst) {
    switch (bpp) {
    case 2: {
        const u16 value = static_cast<u16>(src[0] | (static_cast<u16>(src[1]) << 8));
        const u8 r5 = static_cast<u8>((value >> 11) & 0x1F);
        const u8 g6 = static_cast<u8>((value >> 5) & 0x3F);
        const u8 b5 = static_cast<u8>(value & 0x1F);
        dst[0] = static_cast<u8>((r5 * 255) / 31);
        dst[1] = static_cast<u8>((g6 * 255) / 63);
        dst[2] = static_cast<u8>((b5 * 255) / 31);
        dst[3] = 255;
        break;
    }
    case 3:
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = 255;
        break;
    case 4:
    default:
        dst[0] = src[0];
        dst[1] = src[1];
        dst[2] = src[2];
        dst[3] = src[3];
        break;
    }
}

void ConvertRotateToRgba8888(const u8* src, u8* dst, u32 src_width, u32 src_height, u32 src_stride,
                             u32 dst_width, u32 dst_height) {
    if (!src || !dst || src_width == 0 || src_height == 0 || src_stride == 0) {
        return;
    }

    const u32 bpp = EstimateBytesPerPixel(src_width, src_stride);
    const u32 max_x = std::min(src_width, dst_height);
    const u32 max_y = std::min(src_height, dst_width);

    // Rotate clockwise: source portrait buffers become landscape output.
    for (u32 sy = 0; sy < max_y; ++sy) {
        for (u32 sx = 0; sx < max_x; ++sx) {
            const u32 dx = dst_width - 1 - sy;
            const u32 dy = sx;
            const u8* const src_px = src + (sy * src_stride) + (sx * bpp);
            u8* const dst_px = dst + ((dy * dst_width + dx) * 4);
            DecodePixelToRgba(src_px, bpp, dst_px);
        }
    }
}

} // namespace

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

        auto* const screen_buffer = static_cast<u8*>(state.GetScreenDataBuffer());
        if (!screen_buffer || state.GetScreenDataSize() < ((400 * 240 + 320 * 240) * 4)) {
            LOG_WARNING(Render, "Deko3D screen buffer unavailable or too small");
            return false;
        }
        std::memset(screen_buffer, 0, state.GetScreenDataSize());

        // Copy top screen (400x240 RGB565 or RGBA8)
        {
            const auto& fb0 = framebuffer_config[0];
            const u32 width = fb0.width.Value();
            const u32 height = fb0.height.Value();
            const u32 stride = fb0.stride;

            u8* fb0_ptr = memory.GetPhysicalPointer(fb0_addr);
            if (fb0_ptr && width > 0 && height > 0 && stride > 0) {
                ConvertRotateToRgba8888(fb0_ptr, screen_buffer, width, height, stride, 400, 240);
                SWITCH_TRACE_EVENTF("Deko3D", "Presenter::PresentFrame", "top screen converted",
                                    "width=%u height=%u stride=%u bpp=%u", width, height, stride,
                                    EstimateBytesPerPixel(width, stride));
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
                const u32 top_size = 400 * 240 * 4;
                u8* const bottom_buffer = screen_buffer + top_size;
                ConvertRotateToRgba8888(fb1_ptr, bottom_buffer, width, height, stride, 320, 240);
                SWITCH_TRACE_EVENTF("Deko3D", "Presenter::PresentFrame", "bottom screen converted",
                                    "width=%u height=%u stride=%u bpp=%u", width, height, stride,
                                    EstimateBytesPerPixel(width, stride));
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
