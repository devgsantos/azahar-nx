// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_rasterizer.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

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
    const u32 command_slice_size = RasterCommandMemorySize / FrameSliceCount;
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
        slice.command_offset = index * command_slice_size;
        slice.command_size =
            index == FrameSliceCount - 1 ? RasterCommandMemorySize - slice.command_offset
                                         : command_slice_size;
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

bool Rasterizer::TryDrawHardwareBatch() {
    if (!initialized || !device || !queue || !command_buffer || !vertex_cpu_buffer ||
        vertex_gpu_addr == 0) {
        return false;
    }

    const DkShader* const vertex_shader = shader_cache.GetColorVertexShader();
    const DkShader* const fragment_shader = shader_cache.GetColorFragmentShader();
    const DkImageView* const color_target = state.GetTopScreenRenderTargetView();
    if (!vertex_shader || !fragment_shader || !color_target) {
        return false;
    }

    const std::size_t vertices_per_slice =
        3 * (static_cast<std::size_t>(frame_slices[0].vertex_size) /
             (3 * sizeof(HardwareVertex)));
    if (vertices_per_slice < 3) {
        return false;
    }

    for (std::size_t base_vertex = 0; base_vertex < vertex_batch.size();
         base_vertex += vertices_per_slice) {
        const std::size_t remaining = vertex_batch.size() - base_vertex;
        const std::size_t vertex_count = std::min(vertices_per_slice, remaining);
        const std::size_t aligned_vertex_count = vertex_count - (vertex_count % 3);
        if (aligned_vertex_count == 0) {
            return false;
        }

        FrameSlice& slice = CurrentFrameSlice();
        if (!WaitForFrameSlice(slice)) {
            return false;
        }
        if (!SubmitHardwareChunk(slice, base_vertex, aligned_vertex_count)) {
            return false;
        }
    }

    state.MarkTopScreenGpuDirty();
    return true;
}

bool Rasterizer::SubmitHardwareChunk(FrameSlice& slice, std::size_t base_vertex,
                                     std::size_t vertex_count) {
    const std::size_t vertex_bytes = vertex_count * sizeof(HardwareVertex);
    if (vertex_bytes > slice.vertex_size) {
        return false;
    }

    std::memcpy(static_cast<u8*>(vertex_cpu_buffer) + slice.vertex_offset,
                vertex_batch.data() + base_vertex, vertex_bytes);

    const DkShader* const shaders[] = {shader_cache.GetColorVertexShader(),
                                       shader_cache.GetColorFragmentShader()};
    if (!shaders[0] || !shaders[1]) {
        return false;
    }

    const DkVtxAttribState attribs[] = {
        {0, 0, offsetof(HardwareVertex, position), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, color), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, tex_coord0), DkVtxAttribSize_2x32, DkVtxAttribType_Float,
         0},
        {0, 0, offsetof(HardwareVertex, tex_coord1), DkVtxAttribSize_2x32, DkVtxAttribType_Float,
         0},
        {0, 0, offsetof(HardwareVertex, tex_coord2), DkVtxAttribSize_2x32, DkVtxAttribType_Float,
         0},
        {0, 0, offsetof(HardwareVertex, tex_coord0_w), DkVtxAttribSize_1x32,
         DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, normquat), DkVtxAttribSize_4x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(HardwareVertex, view), DkVtxAttribSize_3x32, DkVtxAttribType_Float, 0},
    };
    const DkVtxBufferState vtx_buffer_state[] = {{sizeof(HardwareVertex), 0}};

    DkRasterizerState rasterizer_state;
    DkMultisampleState multisample_state;
    DkColorState color_state;
    DkColorWriteState color_write_state;
    DkDepthStencilState depth_stencil_state;
    dkRasterizerStateDefaults(&rasterizer_state);
    dkMultisampleStateDefaults(&multisample_state);
    dkColorStateDefaults(&color_state);
    dkColorWriteStateDefaults(&color_write_state);
    dkDepthStencilStateDefaults(&depth_stencil_state);
    rasterizer_state.cullMode = DkFace_None;
    depth_stencil_state.depthTestEnable = false;
    depth_stencil_state.depthWriteEnable = false;

    constexpr u32 TopScreenWidth = 400;
    constexpr u32 TopScreenHeight = 240;
    const DkViewport viewport = {0.0f, 0.0f, static_cast<float>(TopScreenWidth),
                                 static_cast<float>(TopScreenHeight), 0.0f, 1.0f};
    const DkScissor scissor = {0, 0, TopScreenWidth, TopScreenHeight};

    dkCmdBufClear(command_buffer);
    dkCmdBufAddMemory(command_buffer, command_mem_block, slice.command_offset,
                      slice.command_size);
    dkCmdBufBindRenderTarget(command_buffer, state.GetTopScreenRenderTargetView(), nullptr);
    dkCmdBufSetViewports(command_buffer, 0, &viewport, 1);
    dkCmdBufSetScissors(command_buffer, 0, &scissor, 1);
    dkCmdBufBindShaders(command_buffer, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufBindRasterizerState(command_buffer, &rasterizer_state);
    dkCmdBufBindMultisampleState(command_buffer, &multisample_state);
    dkCmdBufBindColorState(command_buffer, &color_state);
    dkCmdBufBindColorWriteState(command_buffer, &color_write_state);
    dkCmdBufBindDepthStencilState(command_buffer, &depth_stencil_state);
    dkCmdBufBindVtxAttribState(command_buffer, attribs,
                               sizeof(attribs) / sizeof(attribs[0]));
    dkCmdBufBindVtxBufferState(command_buffer, vtx_buffer_state,
                               sizeof(vtx_buffer_state) / sizeof(vtx_buffer_state[0]));
    dkCmdBufBindVtxBuffer(command_buffer, 0, vertex_gpu_addr + slice.vertex_offset,
                          static_cast<u32>(vertex_bytes));
    dkCmdBufDraw(command_buffer, DkPrimitive_Triangles, static_cast<u32>(vertex_count), 1, 0, 0);
    dkCmdBufSignalFence(command_buffer, &slice.fence, true);

    const DkCmdList draw_cmd = dkCmdBufFinishList(command_buffer);
    if (!draw_cmd) {
        return false;
    }

    dkQueueSubmitCommands(queue, draw_cmd);
    slice.fence_pending = true;
    RecordHardwareDraw(vertex_count / 3);
    return true;
}
#endif

void Rasterizer::AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                             const Pica::OutputVertex& v2) {
    RasterizerAccelerated::AddTriangle(v0, v1, v2);
    fallback_vertex_batch.push_back(v0);
    fallback_vertex_batch.push_back(v1);
    fallback_vertex_batch.push_back(v2);
}

void Rasterizer::DrawTriangles() {
    if (vertex_batch.empty()) {
        return;
    }

#ifdef __SWITCH__
    RecordHardwareDrawAttempt();
    if (TryDrawHardwareBatch()) {
        vertex_batch.clear();
        fallback_vertex_batch.clear();
        return;
    }
    RecordHardwareDrawFailure();
#endif

    DrawSoftwareFallback();
}

void Rasterizer::DrawSoftwareFallback() {
    LogSoftwareBridgeOnce();
    for (std::size_t index = 0; index + 2 < fallback_vertex_batch.size(); index += 3) {
        software_fallback.AddTriangle(fallback_vertex_batch[index], fallback_vertex_batch[index + 1],
                                      fallback_vertex_batch[index + 2]);
    }
    RecordSoftwareFallback(fallback_vertex_batch.size() / 3);
    vertex_batch.clear();
    fallback_vertex_batch.clear();
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
    fallback_vertex_batch.clear();
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
