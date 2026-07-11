// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <chrono>
#include <cstddef>
#include <limits>

#include "common/logging/log.h"
#include "core/memory.h"
#include "video_core/pica/regs_framebuffer.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/utils.h"

namespace VideoCore::Deko3D {

#ifdef __SWITCH__
namespace {

constexpr u64 MaxResolveBytes = 4ULL * 1024 * 1024;
constexpr u32 ResolveCommandBytes = 16 * 1024;

u32 AlignUp(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

} // namespace

bool ResolveCpuDirtyRenderTarget(void* raw_target, Memory::MemorySystem& memory, State& state) {
    auto* const target = static_cast<State::CachedRenderTarget*>(raw_target);
    if (!target) {
        return false;
    }

    // The DKCR targets observed on hardware are RGBA8. Keep other formats on the existing
    // software path until their exact guest-to-Deko packing has been validated independently.
    if (target->key.format !=
        static_cast<u32>(Pica::FramebufferRegs::ColorFormat::RGBA8)) {
        return false;
    }

    const u32 width = target->key.width;
    const u32 height = target->key.height;
    const u64 logical_bytes = static_cast<u64>(width) * height * 4;
    if (width == 0 || height == 0 || logical_bytes == 0 || logical_bytes > MaxResolveBytes ||
        logical_bytes > std::numeric_limits<u32>::max()) {
        return false;
    }

    const u8* const guest_pixels = memory.GetPhysicalPointer(target->key.color_address);
    const DkDevice device = state.GetDevice();
    const DkQueue queue = state.GetRasterQueue();
    if (!guest_pixels || !device || !queue) {
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
    dkMemBlockMakerDefaults(
        &staging_maker, device,
        AlignUp(static_cast<u32>(logical_bytes), DK_MEMBLOCK_ALIGNMENT));
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

    // The software PICA framebuffer is Morton-tiled and vertically inverted. Deko3D's RGBA8
    // upload buffer is linear, so restore the exact software-rendered image before continuing.
    for (u32 destination_y = 0; destination_y < height; ++destination_y) {
        const u32 source_y = height - 1 - destination_y;
        const u32 coarse_y = source_y & ~7U;
        for (u32 x = 0; x < width; ++x) {
            const std::size_t source_offset =
                VideoCore::GetMortonOffset(x, source_y, 4) +
                static_cast<std::size_t>(coarse_y) * width * 4;
            const std::size_t destination_offset =
                (static_cast<std::size_t>(destination_y) * width + x) * 4;

            // PICA RGBA8 guest storage is A, B, G, R in byte order; Deko3D expects R, G, B, A.
            staging_pixels[destination_offset + 0] = guest_pixels[source_offset + 3];
            staging_pixels[destination_offset + 1] = guest_pixels[source_offset + 2];
            staging_pixels[destination_offset + 2] = guest_pixels[source_offset + 1];
            staging_pixels[destination_offset + 3] = guest_pixels[source_offset + 0];
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

    DkCopyBuf copy_buffer{};
    copy_buffer.addr = staging_gpu;
    copy_buffer.rowLength = 0;
    copy_buffer.imageHeight = 0;

    DkImageRect destination{};
    destination.x = 0;
    destination.y = 0;
    destination.z = 0;
    destination.width = width;
    destination.height = height;
    destination.depth = 1;

    DkFence completion_fence{};
    dkCmdBufCopyBufferToImage(command_buffer, &copy_buffer, &target->view, &destination, 0);
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
                  "Deko3D CPU-dirty render-target resolve failed addr=0x{:08x} result={} "
                  "queue_ok={}",
                  target->key.color_address, static_cast<int>(wait_result), queue_ok);
        dkQueueWaitIdle(queue);
        destroy_resources();
        return false;
    }

    destroy_resources();

    target->needs_clear = false;
    state.MarkRenderTargetGpuDirty(*target);

    static auto last_resolve_log = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last_resolve_log >= std::chrono::seconds(1)) {
        last_resolve_log = now;
        LOG_INFO(Render,
                 "Deko3D CPU->GPU render-target resolve addr=0x{:08x} size={}x{} bytes={}",
                 target->key.color_address, width, height,
                 static_cast<unsigned long long>(logical_bytes));
    }
    return true;
}

#endif

} // namespace VideoCore::Deko3D
