// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <chrono>

#include "common/logging/log.h"
#include "video_core/pica/regs_external.h"
#include "video_core/pica/regs_framebuffer.h"
#include "video_core/renderer_deko3d/deko3d_rasterizer.h"
#include "video_core/renderer_software/sw_blitter.h"

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

    // Keep the generic GPU-only path for transfer classes whose guest-memory coherence has already
    // been established. The exact DKCR lower transfer needs a stricter sequence because presentation
    // may switch between the cached snapshot and a CPU framebuffer upload.
    if (!IsExactLowerRgba8Transfer(config)) {
        return state.RecordDisplayTransfer(input_address, output_address, input_width, input_height,
                                           output_width, output_height, flags);
    }

    const State::RenderTargetKey source_key{
        .color_address = input_address,
        .width = input_width,
        .height = input_height,
        .format = static_cast<u32>(Pica::FramebufferRegs::ColorFormat::RGBA8),
    };
    State::CachedRenderTarget* source = state.GetOrCreateRenderTarget(source_key);
    if (!source) {
        return false;
    }

    if (source->cpu_dirty) {
        // The source is authoritative in guest memory, so perform the ordinary software display
        // transfer first. This populates the real lower framebuffer and preserves a correct CPU
        // fallback even if the GPU snapshot is later invalidated by a legitimate guest write.
        SwRenderer::SwBlitter guest_mirror{memory, this};
        guest_mirror.DisplayTransfer(config);

        // The software transfer marks the source CPU-owned. Upload that same authoritative source
        // back to Deko3D and create the point-in-time destination snapshot used by direct present.
        const bool source_resolved = source->cpu_dirty && ResolveCpuDirtyRenderTarget(*source);
        const bool snapshot_created =
            source_resolved &&
            state.RecordDisplayTransfer(input_address, output_address, input_width, input_height,
                                        output_width, output_height, flags);

        static auto last_mirror_log = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now - last_mirror_log >= std::chrono::seconds(1)) {
            last_mirror_log = now;
            LOG_INFO(Render,
                     "Deko3D lower transfer guest mirror complete dst=0x{:08x} src=0x{:08x} "
                     "size={}x{} resolved={} snapshot={}",
                     output_address, input_address, input_width, input_height, source_resolved,
                     snapshot_created);
        }

        // The transfer is complete in guest memory even when snapshot creation fails. Returning true
        // prevents the outer GPU dispatcher from repeating the software transfer.
        return true;
    }

    // A source already authoritative on the GPU can still use the ordinary snapshot fast path.
    return state.RecordDisplayTransfer(input_address, output_address, input_width, input_height,
                                       output_width, output_height, flags);
#else
    return false;
#endif
}

} // namespace VideoCore::Deko3D
