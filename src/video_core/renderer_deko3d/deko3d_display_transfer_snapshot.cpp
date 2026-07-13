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

    const auto reject = [&](const char* reason) {
        static auto last_reject_log = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now - last_reject_log >= std::chrono::seconds(1)) {
            last_reject_log = now;
            LOG_INFO(Render,
                     "Deko3D display snapshot reject reason={} src=0x{:08x} dst=0x{:08x} "
                     "in={}x{} out={}x{} flags=0x{:08x}",
                     reason, input_address, output_address, input_width, input_height,
                     output_width, output_height, flags);
        }
        return false;
    };

    if (!initialized || !device || !queue || input_address == 0 || output_address == 0 ||
        input_width == 0 || input_height == 0 || output_width == 0 || output_height == 0) {
        return reject("invalid-state-or-dimensions");
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
        return reject("unsupported-transform-or-format");
    }

    CachedRenderTarget* exact_candidate = nullptr;
    u32 source_y = 0;
    for (auto& candidate : render_targets) {
        if (!candidate || candidate->key.width != input_width ||
            candidate->key.height != input_height ||
            candidate->key.format != static_cast<u32>(ColorFormat::RGBA8)) {
            continue;
        }
        const u64 candidate_begin = candidate->key.color_address;
        const u64 candidate_end = candidate_begin + candidate->guest_memory_bytes;
        if (input_address < candidate_begin || input_address >= candidate_end) {
            continue;
        }
        const u64 byte_offset = static_cast<u64>(input_address) - candidate_begin;
        const u64 row_bytes = static_cast<u64>(input_width) * 4;
        if (row_bytes == 0 || byte_offset % row_bytes != 0) {
            continue;
        }
        const u64 candidate_source_y = byte_offset / row_bytes;
        if (candidate_source_y > std::numeric_limits<u32>::max() ||
            candidate_source_y + output_height > candidate->key.height) {
            continue;
        }
        exact_candidate = candidate.get();
        source_y = static_cast<u32>(candidate_source_y);
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
            return reject(exact_candidate ? "cpu-owned-nonidentity-transfer"
                                          : "missing-exact-source-target");
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
        return reject("output-exceeds-source");
    }

    const ColorFormat destination_guest_format =
        output_format == PixelFormat::RGB8 ? ColorFormat::RGB8 : ColorFormat::RGBA8;
    const RenderTargetKey destination_key{
        .color_address = output_address,
        .width = output_width,
        .height = output_height,
        .format = static_cast<u32>(ColorFormat::RGBA8),
    };
    RenderTargetKey destination_guest_key = destination_key;
    destination_guest_key.format = static_cast<u32>(destination_guest_format);
    const u64 destination_bytes = SurfaceBytes(destination_guest_key);
    if (destination_bytes == 0 ||
        RangesOverlapSnapshot(input_address, SurfaceBytes(source->key), output_address,
                              destination_bytes)) {
        return reject(destination_bytes == 0 ? "zero-destination-footprint"
                                             : "source-destination-overlap");
    }

    CachedRenderTarget* destination = GetOrCreateRenderTarget(destination_key);
    if (!destination || destination == source) {
        return reject(destination ? "destination-is-source" : "destination-create-failed");
    }
    // Guest memory may be RGB8 even though deko3d requires RGBA8 image storage. Keep the stable
    // storage/cache identity and track the guest range independently for overlap invalidation.
    destination->guest_memory_bytes = destination_bytes;

    auto& command_slot = snapshot_command_slots[current_snapshot_command_slot];
    current_snapshot_command_slot =
        (current_snapshot_command_slot + 1) % SnapshotCommandSlotCount;

    if (command_slot.fence_pending) {
        dkQueueFlush(queue);
        DkResult wait_result = dkFenceWait(&command_slot.fence, 0);
        if (wait_result != DkResult_Success) {
            wait_result = dkFenceWait(&command_slot.fence, SnapshotFenceTimeoutNs);
        }
        const bool queue_ok = !dkQueueIsInErrorState(queue);
        if (wait_result != DkResult_Success || !queue_ok) {
            LOG_ERROR(Render,
                      "Deko3D display-transfer snapshot slot wait failed src=0x{:08x} "
                      "dst=0x{:08x} size={}x{} result={} queue_ok={}",
                      input_address, output_address, output_width, output_height,
                      static_cast<int>(wait_result), queue_ok);
            return false;
        }
        command_slot.fence_pending = false;
    }

    if (!command_slot.command_mem) {
        DkMemBlockMaker command_maker;
        dkMemBlockMakerDefaults(&command_maker, device,
                                AlignUpSnapshot(SnapshotCommandBytes, DK_MEMBLOCK_ALIGNMENT));
        command_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        command_slot.command_mem = dkMemBlockCreate(&command_maker);
        if (!command_slot.command_mem) {
            return false;
        }

        DkCmdBufMaker command_buffer_maker;
        dkCmdBufMakerDefaults(&command_buffer_maker, device);
        command_slot.command_buffer = dkCmdBufCreate(&command_buffer_maker);
        if (!command_slot.command_buffer) {
            dkMemBlockDestroy(command_slot.command_mem);
            command_slot.command_mem = nullptr;
            return false;
        }
    }

    dkCmdBufClear(command_slot.command_buffer);
    dkCmdBufAddMemory(command_slot.command_buffer, command_slot.command_mem, 0,
                      SnapshotCommandBytes);

    DkImageRect source_rect{0, static_cast<s32>(source_y), 0, output_width, output_height, 1};
    DkImageRect destination_rect{0, 0, 0, output_width, output_height, 1};
    dkCmdBufBarrier(command_slot.command_buffer, DkBarrier_Fragments, DkInvalidateFlags_Image);
    dkCmdBufBlitImage(command_slot.command_buffer, &source->view, &source_rect, &destination->view,
                      &destination_rect,
                      DkBlitFlag_FilterNearest | DkBlitFlag_ModeBlit, 0);

    dkCmdBufSignalFence(command_slot.command_buffer, &command_slot.fence, true);
    const DkCmdList command_list = dkCmdBufFinishList(command_slot.command_buffer);
    if (!command_list) {
        return false;
    }

    dkQueueSubmitCommands(queue, command_list);
    dkQueueFlush(queue);
    const bool queue_ok = !dkQueueIsInErrorState(queue);
    if (!queue_ok) {
        LOG_ERROR(Render,
                  "Deko3D display-transfer snapshot failed src=0x{:08x} dst=0x{:08x} "
                  "size={}x{} queue_ok={}",
                  input_address, output_address, output_width, output_height, queue_ok);
        return false;
    }
    command_slot.fence_pending = true;

    // Any older cached interpretation of the destination guest range is now stale. Keep the newly
    // written snapshot authoritative and leave unrelated render targets untouched.
    for (auto& candidate : render_targets) {
        if (!candidate || candidate.get() == destination || !candidate->gpu_dirty ||
            !RangesOverlapSnapshot(output_address, destination_bytes,
                                   candidate->key.color_address,
                                   candidate->guest_memory_bytes)) {
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
                 "in={}x{} out={}x{} source_y={} flags=0x{:08x} gen={}",
                 output_address, input_address, input_width, input_height, output_width,
                 output_height, source_y, flags, snapshot_generation);
    }
    return true;
}

#endif

} // namespace VideoCore::Deko3D
