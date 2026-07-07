// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_rasterizer.h"

#include "common/logging/log.h"
#include "video_core/renderer_deko3d/deko3d_shader.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

namespace VideoCore::Deko3D {
namespace {

#ifdef __SWITCH__
u32 AlignUp(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}
#endif

void LogSoftwareBridgeOnce() {
    static bool logged = false;
    if (!logged) {
        LOG_INFO(Render,
                 "Deko3D rasterizer: using software PICA rasterization with native Deko3D "
                 "presentation");
        logged = true;
    }
}

} // namespace

Rasterizer::Rasterizer(Memory::MemorySystem& memory, Pica::PicaCore& pica, State& state_,
                       TextureCache& texture_cache_, ShaderCache& shader_cache_)
    : RasterizerAccelerated{memory, pica}, state{state_}, texture_cache{texture_cache_},
      shader_cache{shader_cache_}, software_fallback{memory, pica} {}

bool Rasterizer::Initialize() {
    if (!state.IsInitialized() || !texture_cache.IsInitialized() || !shader_cache.IsInitialized()) {
        LOG_ERROR(Render,
                  "Deko3D rasterizer initialization requested before dependent renderer state");
        return false;
    }
#ifdef __SWITCH__
    if (!InitializeGpuResources()) {
        ShutdownGpuResources();
        return false;
    }
#endif
    initialized = true;
    LOG_INFO(Render, "Deko3D rasterizer initialized with software compatibility fallback");
    return true;
}

void Rasterizer::Shutdown() {
#ifdef __SWITCH__
    ShutdownGpuResources();
#endif
    initialized = false;
}

#ifdef __SWITCH__
bool Rasterizer::InitializeGpuResources() {
    device = state.GetDevice();
    queue = state.GetQueue();
    if (!device || !queue) {
        LOG_ERROR(Render, "Deko3D rasterizer cannot initialize without device and queue");
        return false;
    }

    DkMemBlockMaker command_mem_maker;
    dkMemBlockMakerDefaults(&command_mem_maker, device,
                            AlignUp(RasterCommandMemorySize, DK_MEMBLOCK_ALIGNMENT));
    command_mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    command_mem_block = dkMemBlockCreate(&command_mem_maker);
    if (!command_mem_block) {
        LOG_ERROR(Render, "Deko3D rasterizer command memory allocation failed");
        return false;
    }

    DkCmdBufMaker command_buffer_maker;
    dkCmdBufMakerDefaults(&command_buffer_maker, device);
    command_buffer = dkCmdBufCreate(&command_buffer_maker);
    if (!command_buffer) {
        LOG_ERROR(Render, "Deko3D rasterizer command buffer creation failed");
        return false;
    }
    dkCmdBufAddMemory(command_buffer, command_mem_block, 0, RasterCommandMemorySize);

    DkMemBlockMaker vertex_mem_maker;
    dkMemBlockMakerDefaults(&vertex_mem_maker, device,
                            AlignUp(VertexBufferSize, DK_MEMBLOCK_ALIGNMENT));
    vertex_mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    vertex_mem_block = dkMemBlockCreate(&vertex_mem_maker);
    if (!vertex_mem_block) {
        LOG_ERROR(Render, "Deko3D rasterizer vertex memory allocation failed");
        return false;
    }
    vertex_cpu_buffer = dkMemBlockGetCpuAddr(vertex_mem_block);
    vertex_gpu_addr = dkMemBlockGetGpuAddr(vertex_mem_block);
    if (!vertex_cpu_buffer || vertex_gpu_addr == 0) {
        LOG_ERROR(Render, "Deko3D rasterizer vertex memory mapping failed");
        return false;
    }

    DkMemBlockMaker uniform_mem_maker;
    dkMemBlockMakerDefaults(&uniform_mem_maker, device,
                            AlignUp(UniformBufferSize, DK_MEMBLOCK_ALIGNMENT));
    uniform_mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    uniform_mem_block = dkMemBlockCreate(&uniform_mem_maker);
    if (!uniform_mem_block) {
        LOG_ERROR(Render, "Deko3D rasterizer uniform memory allocation failed");
        return false;
    }
    uniform_cpu_buffer = dkMemBlockGetCpuAddr(uniform_mem_block);
    uniform_gpu_addr = dkMemBlockGetGpuAddr(uniform_mem_block);
    if (!uniform_cpu_buffer || uniform_gpu_addr == 0) {
        LOG_ERROR(Render, "Deko3D rasterizer uniform memory mapping failed");
        return false;
    }

    DkMemBlockMaker descriptor_mem_maker;
    dkMemBlockMakerDefaults(&descriptor_mem_maker, device,
                            AlignUp(DescriptorBufferSize, DK_MEMBLOCK_ALIGNMENT));
    descriptor_mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    descriptor_mem_block = dkMemBlockCreate(&descriptor_mem_maker);
    if (!descriptor_mem_block) {
        LOG_ERROR(Render, "Deko3D rasterizer descriptor memory allocation failed");
        return false;
    }
    descriptor_cpu_buffer = dkMemBlockGetCpuAddr(descriptor_mem_block);
    descriptor_gpu_addr = dkMemBlockGetGpuAddr(descriptor_mem_block);
    if (!descriptor_cpu_buffer || descriptor_gpu_addr == 0) {
        LOG_ERROR(Render, "Deko3D rasterizer descriptor memory mapping failed");
        return false;
    }

    const u32 vertex_slice_size = VertexBufferSize / FrameSliceCount;
    const u32 uniform_slice_size = UniformBufferSize / FrameSliceCount;
    for (u32 index = 0; index < FrameSliceCount; ++index) {
        auto& slice = frame_slices[index];
        slice.vertex_offset = index * vertex_slice_size;
        slice.vertex_size =
            index == FrameSliceCount - 1 ? VertexBufferSize - slice.vertex_offset
                                         : vertex_slice_size;
        slice.uniform_offset = index * uniform_slice_size;
        slice.uniform_size =
            index == FrameSliceCount - 1 ? UniformBufferSize - slice.uniform_offset
                                         : uniform_slice_size;
        slice.fence = {};
        slice.fence_pending = false;
    }
    current_frame_slice = 0;

    LOG_INFO(Render,
             "Deko3D rasterizer GPU resources created: vertex={} uniform={} descriptor={} "
             "slices={}",
             VertexBufferSize, UniformBufferSize, DescriptorBufferSize, FrameSliceCount);
    return true;
}

void Rasterizer::ShutdownGpuResources() {
    if (device || queue || command_buffer || command_mem_block || vertex_mem_block ||
        uniform_mem_block || descriptor_mem_block) {
        state.WaitIdle();
    }

    if (command_buffer) {
        dkCmdBufDestroy(command_buffer);
        command_buffer = nullptr;
    }
    if (command_mem_block) {
        dkMemBlockDestroy(command_mem_block);
        command_mem_block = nullptr;
    }
    if (vertex_mem_block) {
        dkMemBlockDestroy(vertex_mem_block);
        vertex_mem_block = nullptr;
    }
    vertex_cpu_buffer = nullptr;
    vertex_gpu_addr = 0;

    if (uniform_mem_block) {
        dkMemBlockDestroy(uniform_mem_block);
        uniform_mem_block = nullptr;
    }
    uniform_cpu_buffer = nullptr;
    uniform_gpu_addr = 0;

    if (descriptor_mem_block) {
        dkMemBlockDestroy(descriptor_mem_block);
        descriptor_mem_block = nullptr;
    }
    descriptor_cpu_buffer = nullptr;
    descriptor_gpu_addr = 0;

    frame_slices = {};
    current_frame_slice = 0;
    device = {};
    queue = {};
}

Rasterizer::FrameSlice& Rasterizer::CurrentFrameSlice() {
    auto& slice = frame_slices[current_frame_slice];
    current_frame_slice = (current_frame_slice + 1) % FrameSliceCount;
    return slice;
}

bool Rasterizer::WaitForFrameSlice(FrameSlice& slice) {
    if (!slice.fence_pending) {
        return true;
    }

    RecordRingWait();
    constexpr s64 FenceWaitTimeoutNs = 10'000'000'000LL;
    const DkResult result = dkFenceWait(&slice.fence, FenceWaitTimeoutNs);
    if (result != DkResult_Success) {
        LOG_WARNING(Render, "Deko3D rasterizer frame-slice fence wait failed result={}",
                    static_cast<int>(result));
        return false;
    }
    slice.fence_pending = false;
    return true;
}
#endif

void Rasterizer::AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                             const Pica::OutputVertex& v2) {
    LogSoftwareBridgeOnce();
    RasterizerAccelerated::AddTriangle(v0, v1, v2);
    software_fallback.AddTriangle(v0, v1, v2);
}

void Rasterizer::DrawTriangles() {
    if (vertex_batch.empty()) {
        return;
    }
    RecordSoftwareFallback(vertex_batch.size() / 3);
    vertex_batch.clear();
    software_fallback.DrawTriangles();
}

void Rasterizer::FlushAll() {
    software_fallback.FlushAll();
}

void Rasterizer::FlushRegion(PAddr addr, u32 size) {
    software_fallback.FlushRegion(addr, size);
}

void Rasterizer::InvalidateRegion(PAddr addr, u32 size) {
    software_fallback.InvalidateRegion(addr, size);
}

void Rasterizer::FlushAndInvalidateRegion(PAddr addr, u32 size) {
    software_fallback.FlushAndInvalidateRegion(addr, size);
}

void Rasterizer::ClearAll(bool flush) {
    vertex_batch.clear();
    software_fallback.ClearAll(flush);
}

bool Rasterizer::AccelerateDrawBatch(bool is_indexed) {
    if (!initialized) {
        return false;
    }

    // Direct indexed/non-indexed acceleration is intentionally disabled for the first Deko3D
    // rasterizer milestone. The PICA frontend will emit triangles through AddTriangle(), keeping
    // the compatibility fallback correct while the native HardwareVertex path is added.
    (void)is_indexed;
    return false;
}

} // namespace VideoCore::Deko3D
