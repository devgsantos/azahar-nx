// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>

#include "video_core/rasterizer_accelerated.h"
#include "video_core/renderer_software/sw_rasterizer.h"

#ifdef __SWITCH__
#include <deko3d.h>
#endif

namespace Memory {
class MemorySystem;
}

namespace Pica {
class PicaCore;
}

namespace VideoCore::Deko3D {

class ShaderCache;
class State;
class TextureCache;

class Rasterizer final : public VideoCore::RasterizerAccelerated {
public:
    Rasterizer(Memory::MemorySystem& memory, Pica::PicaCore& pica, State& state,
               TextureCache& texture_cache, ShaderCache& shader_cache);

    bool Initialize();
    void Shutdown();

    // Keep the compatibility fallback coherent until native Deko3D draw submission is wired.
    void AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                     const Pica::OutputVertex& v2) override;
    void DrawTriangles() override;
    void FlushAll() override;
    void FlushRegion(PAddr addr, u32 size) override;
    void InvalidateRegion(PAddr addr, u32 size) override;
    void FlushAndInvalidateRegion(PAddr addr, u32 size) override;
    void ClearAll(bool flush) override;
    bool AccelerateDrawBatch(bool is_indexed) override;

private:
#ifdef __SWITCH__
    static constexpr u32 FrameSliceCount = 3;
    static constexpr u32 RasterCommandMemorySize = 64 * 1024;
    static constexpr u32 VertexBufferSize = 1024 * 1024;
    static constexpr u32 UniformBufferSize = 192 * 1024;
    static constexpr u32 DescriptorBufferSize = 64 * 1024;

    struct FrameSlice {
        u32 vertex_offset = 0;
        u32 vertex_size = 0;
        u32 uniform_offset = 0;
        u32 uniform_size = 0;
        DkFence fence{};
        bool fence_pending = false;
    };

    bool InitializeGpuResources();
    void ShutdownGpuResources();
    FrameSlice& CurrentFrameSlice();
    bool WaitForFrameSlice(FrameSlice& slice);
#endif

    State& state;
    TextureCache& texture_cache;
    ShaderCache& shader_cache;
    SwRenderer::RasterizerSoftware software_fallback;
    bool initialized = false;

#ifdef __SWITCH__
    DkDevice device{};
    DkQueue queue{};
    DkMemBlock command_mem_block{};
    DkCmdBuf command_buffer{};
    DkMemBlock vertex_mem_block{};
    void* vertex_cpu_buffer = nullptr;
    DkGpuAddr vertex_gpu_addr = 0;
    DkMemBlock uniform_mem_block{};
    void* uniform_cpu_buffer = nullptr;
    DkGpuAddr uniform_gpu_addr = 0;
    DkMemBlock descriptor_mem_block{};
    void* descriptor_cpu_buffer = nullptr;
    DkGpuAddr descriptor_gpu_addr = 0;
    std::array<FrameSlice, FrameSliceCount> frame_slices{};
    u32 current_frame_slice = 0;
#endif
};

} // namespace VideoCore::Deko3D
