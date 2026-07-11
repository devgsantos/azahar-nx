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

bool HasAnyVisiblePixel(const u8* rgba, u32 width, u32 height) {
    if (!rgba || width == 0 || height == 0) {
        return false;
    }

    constexpr u32 step = 8;
    for (u32 y = 0; y < height; y += step) {
        for (u32 x = 0; x < width; x += step) {
            const u8* const px = rgba + ((y * width + x) * 4);
            if (px[0] != 0 || px[1] != 0 || px[2] != 0) {
                return true;
            }
        }
    }
    return false;
}

bool HasAnyNonZeroSourceByte(const u8* src, u32 width, u32 height, u32 stride, u32 bpp) {
    if (!src || width == 0 || height == 0 || stride == 0) {
        return false;
    }

    const u32 rows = std::min(height, 64u);
    const u32 bytes_per_row = std::min({stride, width * bpp, 2048u});
    for (u32 y = 0; y < rows; y += 4) {
        const u8* const row = src + static_cast<std::size_t>(y) * stride;
        for (u32 x = 0; x < bytes_per_row; ++x) {
            if (row[x] != 0) {
                return true;
            }
        }
    }
    return false;
}

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
            u8* const dst_px = dst + ((dy * dst_width + dx) * 4);
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
    bool top_source_nonzero = false;
    bool bottom_source_nonzero = false;
    u32 top_width_dbg = 0;
    u32 top_height_dbg = 0;
    u32 top_stride_dbg = 0;
    u32 top_bpp_dbg = 0;
    u32 bottom_width_dbg = 0;
    u32 bottom_height_dbg = 0;
    u32 bottom_stride_dbg = 0;
    u32 bottom_bpp_dbg = 0;
    PresentSource source = PresentSource::Unknown;
    try {
        auto& pica_core = system.GPU().PicaCore();
        auto& memory = system.Memory();
        const auto& framebuffer_config = pica_core.regs.framebuffer_config;

        // Get active framebuffer addresses for top and bottom screens
        fb0_addr = framebuffer_config[0].active_fb == 0 ? framebuffer_config[0].address_left1
                                                        : framebuffer_config[0].address_left2;
        fb1_addr = framebuffer_config[1].active_fb == 0 ? framebuffer_config[1].address_left1
                                                        : framebuffer_config[1].address_left2;
        top_changed = !have_last_addrs || last_top_addr != fb0_addr;
        bottom_changed = !have_last_addrs || last_bottom_addr != fb1_addr;
        state.SelectPresentRenderTargets(fb0_addr, fb1_addr);
        const auto cached_target = state.GetSelectedPresentImage();
        const auto cached_bottom_target = state.GetSelectedBottomPresentImage();
        const bool top_use_gpu_blit = cached_target.IsValid();
        const bool bottom_use_gpu_blit = cached_bottom_target.IsValid();
        frame_changed = top_changed || bottom_changed || top_use_gpu_blit ||
                        bottom_use_gpu_blit || state.IsTopScreenGpuDirty();
        if (top_use_gpu_blit) {
            source = PresentSource::CachedRenderTargetBlit;
        } else {
            source = state.IsTopScreenGpuDirty()
                         ? PresentSource::DekoRenderTarget
                         : (frame_changed ? PresentSource::CpuFramebufferUpload
                                          : PresentSource::RepeatedPreviousFrame);
        }

        // Skip CPU readback when the GPU already has dirty cached render targets,
        // because PresentScreenTexturesFrame will blit them directly.

        auto* const screen_buffer = static_cast<u8*>(state.GetScreenDataBuffer());
        if (!screen_buffer ||
            state.GetScreenDataSize() <
                ((TopScreenWidth * TopScreenHeight + BottomScreenWidth * BottomScreenHeight) *
                 BytesPerPixel)) {
            LOG_WARNING(Render, "Deko3D screen buffer unavailable or too small");
            return false;
        }

        // Copy top screen (400x240 RGB565 or RGBA8) only when no GPU blit path exists.
        if (!top_use_gpu_blit) {
            const auto& fb0 = framebuffer_config[0];
            const u32 width = fb0.width.Value();
            const u32 height = fb0.height.Value();
            const u32 stride = fb0.stride;

            top_width_dbg = width;
            top_height_dbg = height;
            top_stride_dbg = stride;
            top_bpp_dbg = Pica::BytesPerPixel(fb0.color_format);
            u8* fb0_ptr = memory.GetPhysicalPointer(fb0_addr);
            top_source_nonzero = HasAnyNonZeroSourceByte(fb0_ptr, width, height, stride, top_bpp_dbg);
            if (fb0_ptr && width > 0 && height > 0 && stride > 0) {
                ConvertRotateToRgba8888(fb0_ptr, screen_buffer, width, height, stride,
                                        fb0.color_format, TopScreenWidth, TopScreenHeight);
            } else {
                ClearRgba8888(screen_buffer, TopScreenWidth, TopScreenHeight);
            }
        }

        // Copy bottom screen (320x240 RGB565 or RGBA8) when no GPU blit path exists.
        if (!bottom_use_gpu_blit) {
            const auto& fb1 = framebuffer_config[1];
            const u32 width = fb1.width.Value();
            const u32 height = fb1.height.Value();
            const u32 stride = fb1.stride;

            bottom_width_dbg = width;
            bottom_height_dbg = height;
            bottom_stride_dbg = stride;
            bottom_bpp_dbg = Pica::BytesPerPixel(fb1.color_format);
            u8* fb1_ptr = memory.GetPhysicalPointer(fb1_addr);
            bottom_source_nonzero =
                HasAnyNonZeroSourceByte(fb1_ptr, width, height, stride, bottom_bpp_dbg);
            const u32 top_size = TopScreenWidth * TopScreenHeight * BytesPerPixel;
            if (fb1_ptr && width > 0 && height > 0 && stride > 0) {
                u8* const bottom_buffer = screen_buffer + top_size;
                ConvertRotateToRgba8888(fb1_ptr, bottom_buffer, width, height, stride,
                                        fb1.color_format, BottomScreenWidth, BottomScreenHeight);
            } else {
                ClearRgba8888(screen_buffer + top_size, BottomScreenWidth, BottomScreenHeight);
            }
        }

        ++frame_counter;
        const u8* const top_rgba = screen_buffer;
        const u8* const bottom_rgba = screen_buffer + (TopScreenWidth * TopScreenHeight * BytesPerPixel);
        const bool top_has_pixels = HasAnyVisiblePixel(top_rgba, TopScreenWidth, TopScreenHeight);
        const bool bottom_has_pixels = HasAnyVisiblePixel(bottom_rgba, BottomScreenWidth, BottomScreenHeight);
        if (!top_has_pixels) {
            ++blank_top_frames;
        }
        if (!bottom_has_pixels) {
            ++blank_bottom_frames;
        }

        if ((frame_counter % 600) == 0 ||
            (!top_has_pixels && !bottom_has_pixels && frame_counter <= 3)) {
            LOG_INFO(Render,
                     "Deko3D source diagnostics: frames={} blank_top={} blank_bottom={} "
                     "fb0=0x{:08x} {}x{} stride={} bpp={} src_nonzero={} rgba_visible={} "
                     "fb1=0x{:08x} {}x{} stride={} bpp={} src_nonzero={} rgba_visible={}",
                     frame_counter, blank_top_frames, blank_bottom_frames, fb0_addr,
                     top_width_dbg, top_height_dbg, top_stride_dbg, top_bpp_dbg,
                     top_source_nonzero, top_has_pixels, fb1_addr, bottom_width_dbg,
                     bottom_height_dbg, bottom_stride_dbg, bottom_bpp_dbg, bottom_source_nonzero,
                     bottom_has_pixels);
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
