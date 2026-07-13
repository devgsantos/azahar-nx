// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_presenter.h"

#include <algorithm>
#include <cstring>

#include "common/color.h"
#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "core/core.h"
#include "core/memory.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/gpu.h"
#include "video_core/pica/pica_core.h"

namespace VideoCore::Deko3D {
namespace {

constexpr u32 TopScreenWidth = 400;
constexpr u32 TopScreenHeight = 240;
constexpr u32 BottomScreenWidth = 320;
constexpr u32 BottomScreenHeight = 240;
constexpr u32 BytesPerPixel = 4;

void DecodePixelToRgba(const u8* src, Pica::PixelFormat format, u8* dst) {
    Common::Vec4<u8> color;
    switch (format) {
    case Pica::PixelFormat::RGBA8:
        color = Common::Color::DecodeRGBA8(src);
        break;
    case Pica::PixelFormat::RGB8:
        color = Common::Color::DecodeRGB8(src);
        break;
    case Pica::PixelFormat::RGB565:
        color = Common::Color::DecodeRGB565(src);
        break;
    case Pica::PixelFormat::RGB5A1:
        color = Common::Color::DecodeRGB5A1(src);
        break;
    case Pica::PixelFormat::RGBA4:
        color = Common::Color::DecodeRGBA4(src);
        break;
    }
    std::memcpy(dst, color.AsArray(), sizeof(color));
}

void ConvertRotateToRgba8888(const u8* src, u8* dst, u32 src_width, u32 src_height, u32 src_stride,
                             Pica::PixelFormat format, u32 dst_width, u32 dst_height) {
    if (!src || !dst || src_width == 0 || src_height == 0 || src_stride == 0) {
        return;
    }

    const u32 bpp = Pica::BytesPerPixel(format);
    if (bpp == 0) {
        return;
    }
    const u32 pixel_stride = src_stride / bpp;
    if (pixel_stride == 0) {
        return;
    }

    const u32 max_x = std::min({src_width, pixel_stride, dst_height});
    const u32 max_y = std::min(src_height, dst_width);

    // Match RendererSoftware::LoadFBToScreenInfo: 3DS LCD buffers are sideways and
    // addressed with a reversed source column.
    for (u32 y = 0; y < max_y; ++y) {
        for (u32 x = 0; x < max_x; ++x) {
            const u8* const src_px =
                src + static_cast<std::size_t>(y * pixel_stride + pixel_stride - x - 1) * bpp;
            const u32 dx = y;
            const u32 dy = x;
            u8* const dst_px = dst + ((dy * dst_width + dx) * BytesPerPixel);
            DecodePixelToRgba(src_px, format, dst_px);
        }
    }
}

void ClearRgba8888(u8* dst, u32 width, u32 height) {
    if (!dst || width == 0 || height == 0) {
        return;
    }
    std::memset(dst, 0, static_cast<std::size_t>(width) * height * BytesPerPixel);
}

} // namespace

Presenter::Presenter(State& state_, Core::System& system_) : state{state_}, system{system_} {}

bool Presenter::PresentFrame() {
    PAddr fb0_addr = 0;
    PAddr fb1_addr = 0;
    bool top_changed = false;
    bool bottom_changed = false;
    bool frame_changed = false;
    PresentSource source = PresentSource::Unknown;

    try {
        auto& pica_core = system.GPU().PicaCore();
        const auto& framebuffer_config = pica_core.regs.framebuffer_config;

        fb0_addr = framebuffer_config[0].active_fb == 0 ? framebuffer_config[0].address_left1
                                                        : framebuffer_config[0].address_left2;
        fb1_addr = framebuffer_config[1].active_fb == 0 ? framebuffer_config[1].address_left1
                                                        : framebuffer_config[1].address_left2;
        top_changed = !have_last_addrs || last_top_addr != fb0_addr;
        bottom_changed = !have_last_addrs || last_bottom_addr != fb1_addr;

        state.SelectPresentRenderTargets(fb0_addr, fb1_addr);
        const auto* const cached_top = state.GetSelectedPresentRenderTarget();
        const auto* const cached_bottom = state.GetSelectedBottomPresentRenderTarget();
        const bool top_gpu = cached_top != nullptr;
        const bool bottom_gpu = cached_bottom != nullptr;

        frame_changed = top_changed || bottom_changed || top_gpu || bottom_gpu ||
                        state.IsTopScreenGpuDirty();
        if (top_gpu || bottom_gpu) {
            source = PresentSource::CachedRenderTargetBlit;
        } else if (state.IsTopScreenGpuDirty()) {
            source = PresentSource::DekoRenderTarget;
        } else {
            source = frame_changed ? PresentSource::CpuFramebufferUpload
                                   : PresentSource::RepeatedPreviousFrame;
        }

        // A fully hardware-rendered frame never touches guest framebuffer memory or performs the
        // CPU pixel scans that were previously executed for diagnostics on every presentation.
        if (!top_gpu || !bottom_gpu) {
            auto* const screen_buffer = static_cast<u8*>(state.GetScreenDataBuffer());
            constexpr u32 top_size = TopScreenWidth * TopScreenHeight * BytesPerPixel;
            constexpr u32 required_size =
                (TopScreenWidth * TopScreenHeight + BottomScreenWidth * BottomScreenHeight) *
                BytesPerPixel;
            if (!screen_buffer || state.GetScreenDataSize() < required_size) {
                LOG_WARNING(Render, "Deko3D screen buffer unavailable or too small");
                return false;
            }

            auto& memory = system.Memory();
            if (!top_gpu) {
                const auto& fb0 = framebuffer_config[0];
                const u32 width = fb0.width.Value();
                const u32 height = fb0.height.Value();
                const u32 stride = fb0.stride;
                const u8* const fb0_ptr = memory.GetPhysicalPointer(fb0_addr);
                if (fb0_ptr && width > 0 && height > 0 && stride > 0) {
                    ConvertRotateToRgba8888(fb0_ptr, screen_buffer, width, height, stride,
                                            fb0.color_format, TopScreenWidth, TopScreenHeight);
                } else {
                    ClearRgba8888(screen_buffer, TopScreenWidth, TopScreenHeight);
                }
            }

            if (!bottom_gpu) {
                const auto& fb1 = framebuffer_config[1];
                const u32 width = fb1.width.Value();
                const u32 height = fb1.height.Value();
                const u32 stride = fb1.stride;
                const u8* const fb1_ptr = memory.GetPhysicalPointer(fb1_addr);
                u8* const bottom_buffer = screen_buffer + top_size;
                if (fb1_ptr && width > 0 && height > 0 && stride > 0) {
                    ConvertRotateToRgba8888(fb1_ptr, bottom_buffer, width, height, stride,
                                            fb1.color_format, BottomScreenWidth,
                                            BottomScreenHeight);
                } else {
                    ClearRgba8888(bottom_buffer, BottomScreenWidth, BottomScreenHeight);
                }
            }
        }
    } catch (const std::exception& e) {
        LOG_WARNING(Render, "Deko3D Presenter framebuffer access error: {}", e.what());
    }

    const bool presented = state.PresentScreenTexturesFrame();
    if (presented) {
        RecordPresent(source, frame_changed, top_changed || state.IsTopScreenGpuDirty(),
                      bottom_changed);
        last_top_addr = fb0_addr;
        last_bottom_addr = fb1_addr;
        have_last_addrs = true;
    }
    if (presented && !presented_frame) {
        LOG_INFO(Render, "Deko3D first frame presented");
        SWITCH_TRACE_EVENT("Deko3D", "Presenter::PresentFrame", "first frame presented");
        presented_frame = true;
    }
    return presented;
}

} // namespace VideoCore::Deko3D
