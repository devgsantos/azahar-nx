// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <chrono>
#include <cstddef>
#include <cstring>
#include <limits>

#include "common/logging/log.h"
#include "core/memory.h"
#include "video_core/pica/pica_core.h"
#include "video_core/pica/regs_framebuffer.h"
#include "video_core/renderer_deko3d/deko3d_rasterizer.h"
#include "video_core/utils.h"

// deko3d_rasterizer.h replaces infinite waits with deferred frame-slice synchronization for normal
// draws. A temporary resolve allocation must remain alive until its copy completes, so this source
// deliberately uses the real deko3d fence wait.
#ifdef dkFenceWait
#undef dkFenceWait
#endif

namespace VideoCore::Deko3D {

#ifdef __SWITCH__
namespace {

constexpr u64 MaxResolveBytes = 8ULL * 1024 * 1024;
constexpr u32 ResolveCommandBytes = 16 * 1024;
constexpr u32 ResolvePlaneAlignment = 256;

u32 AlignUp(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

} // namespace

bool Rasterizer::ResolveCpuDirtyRenderTarget(State::CachedRenderTarget& target) {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    using DepthFormat = Pica::FramebufferRegs::DepthFormat;

    // The DKCR targets observed on hardware are RGBA8. Keep other color formats on the existing
    // software path until their guest-to-Deko packing has been validated independently.
    if (target.key.format != static_cast<u32>(ColorFormat::RGBA8)) {
        return false;
    }

    const auto& framebuffer = regs.framebuffer.framebuffer;
    const u32 width = target.key.width;
    const u32 height = target.key.height;
    if (width == 0 || height == 0 || target.key.color_address == 0 ||
        framebuffer.GetColorBufferPhysicalAddress() != target.key.color_address ||
        framebuffer.GetWidth() != width || framebuffer.GetHeight() != height) {
        return false;
    }

    const u64 color_bytes_64 = static_cast<u64>(width) * height * 4;
    if (color_bytes_64 == 0 || color_bytes_64 > std::numeric_limits<u32>::max()) {
        return false;
    }
    const u32 color_bytes = static_cast<u32>(color_bytes_64);

    const bool depth_active = regs.framebuffer.output_merger.depth_test_enable != 0 ||
                              regs.framebuffer.output_merger.depth_write_enable != 0;
    const DkImageView* depth_view = nullptr;
    const u8* guest_depth = nullptr;
    u32 guest_depth_bytes_per_pixel = 0;
    u32 host_depth_bytes_per_pixel = 0;
    u32 depth_offset = 0;
    u32 depth_bytes = 0;

    if (depth_active) {
        depth_view = GetOrCreateDepthTarget();
        if (!depth_view || !active_depth_target) {
            return false;
        }

        switch (framebuffer.depth_format) {
        case DepthFormat::D16:
            guest_depth_bytes_per_pixel = 2;
            host_depth_bytes_per_pixel = 2;
            break;
        case DepthFormat::D24:
            guest_depth_bytes_per_pixel = 3;
            // D24 render targets use Z24S8 on Deko3D; synthesize a zero stencil byte.
            host_depth_bytes_per_pixel = 4;
            break;
        case DepthFormat::D24S8:
            guest_depth_bytes_per_pixel = 4;
            host_depth_bytes_per_pixel = 4;
            break;
        }

        if (guest_depth_bytes_per_pixel == 0 || host_depth_bytes_per_pixel == 0 ||
            framebuffer.GetDepthBufferPhysicalAddress() == 0) {
            return false;
        }
        guest_depth = memory.GetPhysicalPointer(framebuffer.GetDepthBufferPhysicalAddress());
        if (!guest_depth) {
            return false;
        }

        depth_offset = AlignUp(color_bytes, ResolvePlaneAlignment);
        const u64 depth_bytes_64 =
            static_cast<u64>(width) * height * host_depth_bytes_per_pixel;
        if (depth_bytes_64 == 0 || depth_bytes_64 > std::numeric_limits<u32>::max()) {
            return false;
        }
        depth_bytes = static_cast<u32>(depth_bytes_64);
    }

    const u64 total_bytes_64 =
        depth_active ? static_cast<u64>(depth_offset) + depth_bytes : color_bytes;
    if (total_bytes_64 == 0 || total_bytes_64 > MaxResolveBytes ||
        total_bytes_64 > std::numeric_limits<u32>::max()) {
        return false;
    }
    const u32 total_bytes = static_cast<u32>(total_bytes_64);

    const u8* const guest_color = memory.GetPhysicalPointer(target.key.color_address);
    if (!guest_color || !device || !queue) {
        return false;
    }

    DkMemBlock staging_mem{};
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
        if (staging_mem) {
            dkMemBlockDestroy(staging_mem);
            staging_mem = nullptr;
        }
    };

    DkMemBlockMaker staging_maker;
    dkMemBlockMakerDefaults(&staging_maker, device,
                            AlignUp(total_bytes, DK_MEMBLOCK_ALIGNMENT));
    staging_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    staging_mem = dkMemBlockCreate(&staging_maker);
    if (!staging_mem) {
        return false;
    }

    auto* const staging_pixels = static_cast<u8*>(dkMemBlockGetCpuAddr(staging_mem));
    const DkGpuAddr staging_gpu = dkMemBlockGetGpuAddr(staging_mem);
    if (!staging_pixels || staging_gpu == 0) {
        destroy_resources();
        return false;
    }

    // Software PICA framebuffers are Morton-tiled and vertically inverted. Restore a linear RGBA8
    // image for Deko3D while preserving the exact software-rendered color contents.
    for (u32 destination_y = 0; destination_y < height; ++destination_y) {
        const u32 source_y = height - 1 - destination_y;
        const u32 coarse_y = source_y & ~7U;
        for (u32 x = 0; x < width; ++x) {
            const std::size_t source_offset =
                VideoCore::GetMortonOffset(x, source_y, 4) +
                static_cast<std::size_t>(coarse_y) * width * 4;
            const std::size_t destination_offset =
                (static_cast<std::size_t>(destination_y) * width + x) * 4;

            // PICA RGBA8 guest storage is A, B, G, R; Deko3D expects R, G, B, A.
            staging_pixels[destination_offset + 0] = guest_color[source_offset + 3];
            staging_pixels[destination_offset + 1] = guest_color[source_offset + 2];
            staging_pixels[destination_offset + 2] = guest_color[source_offset + 1];
            staging_pixels[destination_offset + 3] = guest_color[source_offset + 0];
        }
    }

    if (depth_active) {
        auto* const staging_depth = staging_pixels + depth_offset;
        for (u32 destination_y = 0; destination_y < height; ++destination_y) {
            const u32 source_y = height - 1 - destination_y;
            const u32 coarse_y = source_y & ~7U;
            for (u32 x = 0; x < width; ++x) {
                const std::size_t source_offset =
                    VideoCore::GetMortonOffset(x, source_y, guest_depth_bytes_per_pixel) +
                    static_cast<std::size_t>(coarse_y) * width *
                        guest_depth_bytes_per_pixel;
                const std::size_t destination_offset =
                    (static_cast<std::size_t>(destination_y) * width + x) *
                    host_depth_bytes_per_pixel;

                if (framebuffer.depth_format == DepthFormat::D24) {
                    staging_depth[destination_offset + 0] = guest_depth[source_offset + 0];
                    staging_depth[destination_offset + 1] = guest_depth[source_offset + 1];
                    staging_depth[destination_offset + 2] = guest_depth[source_offset + 2];
                    staging_depth[destination_offset + 3] = 0;
                } else {
                    std::memcpy(staging_depth + destination_offset, guest_depth + source_offset,
                                guest_depth_bytes_per_pixel);
                }
            }
        }
    }

    DkMemBlockMaker command_maker;
    dkMemBlockMakerDefaults(&command_maker, device,
                            AlignUp(ResolveCommandBytes, DK_MEMBLOCK_ALIGNMENT));
    command_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    command_mem = dkMemBlockCreate(&command_maker);
    if (!command_mem) {
        destroy_resources();
        return false;
    }

    DkCmdBufMaker command_buffer_maker;
    dkCmdBufMakerDefaults(&command_buffer_maker, device);
    command_buffer = dkCmdBufCreate(&command_buffer_maker);
    if (!command_buffer) {
        destroy_resources();
        return false;
    }
    dkCmdBufAddMemory(command_buffer, command_mem, 0, ResolveCommandBytes);

    DkImageRect destination{};
    destination.x = 0;
    destination.y = 0;
    destination.z = 0;
    destination.width = width;
    destination.height = height;
    destination.depth = 1;

    DkCopyBuf color_copy{};
    color_copy.addr = staging_gpu;
    color_copy.rowLength = 0;
    color_copy.imageHeight = 0;
    dkCmdBufCopyBufferToImage(command_buffer, &color_copy, &target.view, &destination, 0);

    if (depth_active) {
        DkCopyBuf depth_copy{};
        depth_copy.addr = staging_gpu + depth_offset;
        depth_copy.rowLength = 0;
        depth_copy.imageHeight = 0;
        dkCmdBufCopyBufferToImage(command_buffer, &depth_copy, depth_view, &destination, 0);
    }

    DkFence completion_fence{};
    dkCmdBufSignalFence(command_buffer, &completion_fence, true);
    const DkCmdList command_list = dkCmdBufFinishList(command_buffer);
    if (!command_list) {
        destroy_resources();
        return false;
    }

    dkQueueSubmitCommands(queue, command_list);
    dkQueueFlush(queue);
    const DkResult wait_result = dkFenceWait(&completion_fence, -1);
    const bool queue_ok = !dkQueueIsInErrorState(queue);
    if (wait_result != DkResult_Success || !queue_ok) {
        LOG_ERROR(Render,
                  "Deko3D CPU-dirty render-target resolve failed color=0x{:08x} depth=0x{:08x} "
                  "result={} queue_ok={}",
                  target.key.color_address, framebuffer.GetDepthBufferPhysicalAddress(),
                  static_cast<int>(wait_result), queue_ok);
        dkQueueWaitIdle(queue);
        destroy_resources();
        return false;
    }

    destroy_resources();

    target.needs_clear = false;
    if (depth_active && active_depth_target) {
        active_depth_target->needs_clear = false;
    }
    state.MarkRenderTargetGpuDirty(target);

    static auto last_resolve_log = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last_resolve_log >= std::chrono::seconds(1)) {
        last_resolve_log = now;
        LOG_INFO(Render,
                 "Deko3D CPU->GPU render-target resolve color=0x{:08x} depth=0x{:08x} "
                 "size={}x{} color_bytes={} depth_bytes={}",
                 target.key.color_address,
                 depth_active ? framebuffer.GetDepthBufferPhysicalAddress() : 0, width, height,
                 color_bytes, depth_bytes);
    }
    return true;
}

#endif

} // namespace VideoCore::Deko3D
