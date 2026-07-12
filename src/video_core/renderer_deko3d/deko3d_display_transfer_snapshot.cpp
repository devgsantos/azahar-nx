// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <limits>

#include "common/logging/log.h"
#include "video_core/pica/regs_external.h"
#include "video_core/pica/regs_framebuffer.h"
#include "video_core/renderer_deko3d/deko3d_state.h"

namespace VideoCore::Deko3D {

#ifdef __SWITCH__
namespace {

constexpr u32 SnapshotCommandBytes = 16 * 1024;
constexpr s64 SnapshotFenceTimeoutNs = 1'000'000'000LL;

u32 AlignUpSnapshot(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

u64 SurfaceBytes(const State::RenderTargetKey& key) {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    u32 bytes_per_pixel = 4;
    switch (static_cast<ColorFormat>(key.format)) {
    case ColorFormat::RGB8:
        bytes_per_pixel = 3;
        break;
    case ColorFormat::RGB5A1:
    case ColorFormat::RGB565:
    case ColorFormat::RGBA4:
        bytes_per_pixel = 2;
        break;
    case ColorFormat::RGBA8:
    default:
        bytes_per_pixel = 4;
        break;
    }
    return static_cast<u64>(key.width) * key.height * bytes_per_pixel;
}

bool RangesOverlapSnapshot(PAddr lhs, u64 lhs_size, PAddr rhs, u64 rhs_size) {
    if (lhs_size == 0 || rhs_size == 0) {
        return false;
    }
    const u64 lhs_begin = lhs;
    const u64 lhs_end = lhs_begin + lhs_size;
    const u64 rhs_begin = rhs;
    const u64 rhs_end = rhs_begin + rhs_size;
    return lhs_begin < rhs_end && rhs_begin < lhs_end;
}

} // namespace

bool State::RecordDisplayTransfer(PAddr input_address, PAddr output_address, u32 input_width,
                                  u32 input_height, u32 output_width, u32 output_height,
                                  u32 flags) {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    using PixelFormat = Pica::PixelFormat;
    using ScalingMode = Pica::DisplayTransferConfig::ScalingMode;

    if (!initialized || !device || !queue || input_address == 0 || output_address == 0 ||
        input_width == 0 || input_height == 0 || output_width == 0 || output_height == 0) {
        return false;
    }

    const bool flip_vertically = (flags & (1U << 0)) != 0;
    const bool block_32 = (flags & (1U << 16)) != 0;
    const auto input_format = static_cast<PixelFormat>((flags >> 8) & 0x7U);
    const auto output_format = static_cast<PixelFormat>((flags >> 12) & 0x7U);
    const auto scaling = static_cast<ScalingMode>((flags >> 24) & 0x3U);

    // The first correctness milestone intentionally supports only the exact class observed in DKCR:
    // an RGBA8 GPU render target copied/cropped without scaling into an RGBA8/RGB8 display buffer.
    // Unsupported transforms continue through the software blitter.
    if (flip_vertically || block_32 || scaling != ScalingMode::NoScale ||
        input_format != PixelFormat::RGBA8 ||
        (output_format != PixelFormat::RGBA8 && output_format != PixelFormat::RGB8)) {
        return false;
    }

    CachedRenderTarget* exact_candidate = nullptr;
    for (auto& candidate : render_targets) {
        if (!candidate || candidate->key.color_address != input_address ||
            candidate->key.width != input_width || candidate->key.height != input_height ||
            candidate->key.format != static_cast<u32>(ColorFormat::RGBA8)) {
            continue;
        }
        exact_candidate = candidate.get();
        break;
    }

    CachedRenderTarget* source = exact_candidate;
    if (!source || !source->gpu_dirty ||
        (source->owner != SurfaceOwner::Deko3D &&
         source->owner != SurfaceOwner::DisplayTransfer)) {
        // DKCR issues the exact 240x320 lower-screen display transfer while the source is still
        // CPU-owned. The software transfer must still run, but retain the exact 1:1 relationship so
        // presentation can use the source after ResolveCpuDirtyRenderTarget promotes it to Deko3D.
        // Crops, scaling and format conversions are deliberately excluded from this deferred path.
        if (!exact_candidate || output_width != input_width || output_height != input_height) {
            return false;
        }

        auto existing =
            std::find_if(display_transfer_targets.begin(), display_transfer_targets.end(),
                         [output_address](const auto& entry) {
                             return entry.display_address == output_address;
                         });
        const u64 deferred_generation = std::max<u64>(1, exact_candidate->deko_generation);
        if (existing != display_transfer_targets.end()) {
            existing->target = exact_candidate;
            existing->deko_generation = deferred_generation;
            existing->input_width = input_width;
            existing->input_height = input_height;
            existing->output_width = output_width;
            existing->output_height = output_height;
            existing->flags = flags;
        } else {
            display_transfer_targets.emplace_back(DisplayTransferTarget{
                .display_address = output_address,
                .target = exact_candidate,
                .deko_generation = deferred_generation,
                .input_width = input_width,
                .input_height = input_height,
                .output_width = output_width,
                .output_height = output_height,
                .flags = flags,
            });
        }

        static auto last_deferred_log = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now - last_deferred_log >= std::chrono::seconds(1)) {
            last_deferred_log = now;
            LOG_INFO(Render,
                     "Deko3D deferred exact display mapping dst=0x{:08x} src=0x{:08x} "
                     "size={}x{} owner={} cpu_dirty={} gpu_dirty={}",
                     output_address, input_address, input_width, input_height,
                     static_cast<u32>(exact_candidate->owner), exact_candidate->cpu_dirty,
                     exact_candidate->gpu_dirty);
        }
        return false;
    }

    // No-scale transfers may crop the source. They must never read beyond the live GPU image.
    if (output_width > source->key.width || output_height > source->key.height) {
        return false;
    }

    const RenderTargetKey destination_key{
        .color_address = output_address,
        .width = output_width,
        .height = output_height,
        .format = static_cast<u32>(ColorFormat::RGBA8),
    };
    const u64 destination_bytes = SurfaceBytes(destination_key);
    if (destination_bytes == 0 ||
        RangesOverlapSnapshot(input_address, SurfaceBytes(source->key), output_address,
                              destination_bytes)) {
        return false;
    }

    CachedRenderTarget* destination = GetOrCreateRenderTarget(destination_key);
    if (!destination || destination == source) {
        return false;
    }

    DkMemBlock command_mem{};
    DkCmdBuf command_buffer{};
    const auto destroy_resources = [&] {
        if (command_buffer) {
            dkCmdBufDestroy(command_buffer);
            command_buffer = nullptr;
        }
        if (command_mem) {
            dkMemBlockDestroy(command_mem);
            command_mem = nullptr;
        }
    };

    DkMemBlockMaker command_maker;
    dkMemBlockMakerDefaults(&command_maker, device,
                            AlignUpSnapshot(SnapshotCommandBytes, DK_MEMBLOCK_ALIGNMENT));
    command_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    command_mem = dkMemBlockCreate(&command_maker);
    if (!command_mem) {
        return false;
    }

    DkCmdBufMaker command_buffer_maker;
    dkCmdBufMakerDefaults(&command_buffer_maker, device);
    command_buffer = dkCmdBufCreate(&command_buffer_maker);
    if (!command_buffer) {
        destroy_resources();
        return false;
    }
    dkCmdBufAddMemory(command_buffer, command_mem, 0, SnapshotCommandBytes);

    DkImageRect source_rect{0, 0, 0, output_width, output_height, 1};
    DkImageRect destination_rect{0, 0, 0, output_width, output_height, 1};
    dkCmdBufBarrier(command_buffer, DkBarrier_Fragments, DkInvalidateFlags_Image);
    dkCmdBufBlitImage(command_buffer, &source->view, &source_rect, &destination->view,
                      &destination_rect,
                      DkBlitFlag_FilterNearest | DkBlitFlag_ModeBlit, 0);

    DkFence completion_fence{};
    dkCmdBufSignalFence(command_buffer, &completion_fence, true);
    const DkCmdList command_list = dkCmdBufFinishList(command_buffer);
    if (!command_list) {
        destroy_resources();
        return false;
    }

    dkQueueSubmitCommands(queue, command_list);
    dkQueueFlush(queue);
    const DkResult wait_result = dkFenceWait(&completion_fence, SnapshotFenceTimeoutNs);
    const bool queue_ok = !dkQueueIsInErrorState(queue);
    if (wait_result != DkResult_Success || !queue_ok) {
        LOG_ERROR(Render,
                  "Deko3D display-transfer snapshot failed src=0x{:08x} dst=0x{:08x} "
                  "size={}x{} result={} queue_ok={}",
                  input_address, output_address, output_width, output_height,
                  static_cast<int>(wait_result), queue_ok);
        dkQueueWaitIdle(queue);
        destroy_resources();
        return false;
    }
    destroy_resources();

    // Any older cached interpretation of the destination guest range is now stale. Keep the newly
    // written snapshot authoritative and leave unrelated render targets untouched.
    for (auto& candidate : render_targets) {
        if (!candidate || candidate.get() == destination || !candidate->gpu_dirty ||
            !RangesOverlapSnapshot(output_address, destination_bytes,
                                   candidate->key.color_address, SurfaceBytes(candidate->key))) {
            continue;
        }
        candidate->gpu_dirty = false;
        candidate->cpu_dirty = true;
        candidate->owner = SurfaceOwner::DisplayTransfer;
        candidate->guest_memory_generation = ++render_target_generation;
        RecordRenderTargetCpuDirty();
    }

    const u64 snapshot_generation = ++render_target_generation;
    destination->owner = SurfaceOwner::DisplayTransfer;
    destination->gpu_dirty = true;
    destination->cpu_dirty = false;
    destination->needs_clear = false;
    destination->display_transfer_generation = snapshot_generation;
    destination->deko_generation = snapshot_generation;
    RecordRenderTargetGpuDirty();

    auto existing =
        std::find_if(display_transfer_targets.begin(), display_transfer_targets.end(),
                     [output_address](const auto& entry) {
                         return entry.display_address == output_address;
                     });
    if (existing != display_transfer_targets.end()) {
        existing->target = destination;
        existing->deko_generation = snapshot_generation;
        existing->input_width = input_width;
        existing->input_height = input_height;
        existing->output_width = output_width;
        existing->output_height = output_height;
        existing->flags = flags;
    } else {
        display_transfer_targets.emplace_back(DisplayTransferTarget{
            .display_address = output_address,
            .target = destination,
            .deko_generation = snapshot_generation,
            .input_width = input_width,
            .input_height = input_height,
            .output_width = output_width,
            .output_height = output_height,
            .flags = flags,
        });
    }

    static auto last_snapshot_log = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last_snapshot_log >= std::chrono::seconds(1)) {
        last_snapshot_log = now;
        LOG_INFO(Render,
                 "Deko3D display-transfer snapshot dst=0x{:08x} src=0x{:08x} "
                 "in={}x{} out={}x{} flags=0x{:08x} gen={}",
                 output_address, input_address, input_width, input_height, output_width,
                 output_height, flags, snapshot_generation);
    }
    return true;
}

#endif

} // namespace VideoCore::Deko3D
