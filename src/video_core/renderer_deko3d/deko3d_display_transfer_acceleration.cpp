// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <chrono>

#include "common/logging/log.h"
#include "video_core/pica/regs_external.h"
#include "video_core/pica/regs_framebuffer.h"
#include "video_core/renderer_deko3d/deko3d_rasterizer.h"

namespace VideoCore::Deko3D {

#ifdef __SWITCH__
namespace {

bool IsExactLowerRgba8Transfer(const Pica::DisplayTransferConfig& config) {
    using PixelFormat = Pica::PixelFormat;
    using ScalingMode = Pica::DisplayTransferConfig::ScalingMode;

    const u32 flags = config.flags;
    const bool flip_vertically = (flags & (1U << 0)) != 0;
    const bool block_32 = (flags & (1U << 16)) != 0;
    const auto input_format = static_cast<PixelFormat>((flags >> 8) & 0x7U);
    const auto output_format = static_cast<PixelFormat>((flags >> 12) & 0x7U);
    const auto scaling = static_cast<ScalingMode>((flags >> 24) & 0x3U);

    return !flip_vertically && !block_32 && scaling == ScalingMode::NoScale &&
           input_format == PixelFormat::RGBA8 &&
           (output_format == PixelFormat::RGBA8 || output_format == PixelFormat::RGB8) &&
           config.input_width.Value() == 240 && config.input_height.Value() == 320 &&
           config.output_width.Value() == 240 && config.output_height.Value() == 320;
}

} // namespace
#endif

bool Rasterizer::AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) {
#ifdef __SWITCH__
    const PAddr input_address = config.GetPhysicalInputAddress();
    const PAddr output_address = config.GetPhysicalOutputAddress();
    const u32 input_width = config.input_width.Value();
    const u32 input_height = config.input_height.Value();
    const u32 output_width = config.output_width.Value();
    const u32 output_height = config.output_height.Value();
    const u32 flags = config.flags;

    // Fast path for a source that is already authoritative on the GPU.
    if (state.RecordDisplayTransfer(input_address, output_address, input_width, input_height,
                                    output_width, output_height, flags)) {
        return true;
    }

    // DKCR's lower-screen transfer reaches this method after the shared source has been invalidated
    // back to guest memory. Resolve that exact 240x320 source again at transfer time, then perform a
    // real point-in-time GPU snapshot. The packed/cropped upper-screen transfer is intentionally
    // excluded and continues through the software blitter.
    if (!IsExactLowerRgba8Transfer(config)) {
        return false;
    }

    const State::RenderTargetKey source_key{
        .color_address = input_address,
        .width = input_width,
        .height = input_height,
        .format = static_cast<u32>(Pica::FramebufferRegs::ColorFormat::RGBA8),
    };
    State::CachedRenderTarget* source = state.GetOrCreateRenderTarget(source_key);
    if (!source || !source->cpu_dirty || !ResolveCpuDirtyRenderTarget(*source)) {
        return false;
    }

    const bool snapshot_created =
        state.RecordDisplayTransfer(input_address, output_address, input_width, input_height,
                                    output_width, output_height, flags);
    if (snapshot_created) {
        static auto last_retry_log = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now - last_retry_log >= std::chrono::seconds(1)) {
            last_retry_log = now;
            LOG_INFO(Render,
                     "Deko3D transfer-time lower resolve+snapshot dst=0x{:08x} src=0x{:08x} "
                     "size={}x{}",
                     output_address, input_address, input_width, input_height);
        }
    }
    return snapshot_created;
#else
    return false;
#endif
}

} // namespace VideoCore::Deko3D
