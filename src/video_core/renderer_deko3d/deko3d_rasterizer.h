// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <deque>
#include <optional>
#include <unordered_set>
#include <vector>

#include "video_core/pica/output_vertex.h"
#include "video_core/rasterizer_accelerated.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"
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

struct CachedTexture;
class ShaderCache;
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
    bool AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) override;
    bool AccelerateDrawBatch(bool is_indexed) override;

private:
#ifdef __SWITCH__
    static constexpr u32 FrameSliceCount = 3;
    static constexpr u32 RasterCommandMemorySize = 64 * 1024;
    static constexpr u32 VertexBufferSize = 1024 * 1024;
    static constexpr u32 UniformBufferSize = 192 * 1024;
    static constexpr u32 DescriptorBufferSize = 64 * 1024;

    struct FrameSlice {
        DkCmdBuf command_buffer{};
        DkMemBlock command_mem_block{};
        u32 command_offset = 0;
        u32 command_size = 0;
        u32 vertex_offset = 0;
        u32 vertex_size = 0;
        u32 uniform_offset = 0;
        u32 uniform_size = 0;
        u32 descriptor_offset = 0;
        u32 descriptor_size = 0;
        DkFence fence{};
        bool fence_pending = false;
        std::size_t pending_vertices = 0;
    };

    struct HardwareEligibility {
        bool supported = false;
        FallbackReason reason = FallbackReason::UnsupportedState;
        u32 blockers = 0;
    };

    struct DepthTarget {
        DkMemBlock mem_block{};
        DkImage image{};
        DkImageView view{};
        u32 width = 0;
        u32 height = 0;
        u32 format = 0;
        bool needs_clear = true;
    };

    enum EligibilityBlocker : u32 {
        None = 0,
        InvalidBatch = 1U << 0,
        MissingGpuResources = 1U << 1,
        ShaderUnavailable = 1U << 2,
        WrongRenderTarget = 1U << 3,
        FramebufferFormat = 1U << 4,
        FramebufferDimensions = 1U << 5,
        TexturesEnabled = 1U << 6,
        DepthTestEnabled = 1U << 7,
        DepthWriteEnabled = 1U << 8,
        StencilEnabled = 1U << 9,
        BlendingEnabled = 1U << 10,
        AlphaTestUnsupported = 1U << 11,
        LogicOpUnsupported = 1U << 12,
        ColorMaskUnsupported = 1U << 13,
        CullModeUnsupported = 1U << 14,
        ViewportUnsupported = 1U << 15,
        ScissorUnsupported = 1U << 16,
        ShadowRendering = 1U << 17,
        ProceduralTexture = 1U << 18,
    };

    enum DirectEligibilityBlocker : u32 {
        DirectUnimplemented = 1U << 0,
        DirectTopology = 1U << 1,
        DirectGeometryShader = 1U << 2,
        DirectVertexFormat = 1U << 3,
        DirectIndexFormat = 1U << 4,
        DirectOther = 1U << 5,
    };

    bool InitializeGpuResources();
    void ShutdownGpuResources();
    FrameSlice& CurrentFrameSlice();
    bool WaitForFrameSlice(FrameSlice& slice);
    HardwareEligibility EvaluateTransformedBatchEligibility() const;
    HardwareEligibility EvaluateDirectBatchEligibility(bool is_indexed) const;
    bool TryDrawHardwareBatch(std::size_t& submitted_vertices);
    bool SubmitHardwareChunk(FrameSlice& slice, State::CachedRenderTarget& color_target,
                             const DkImageView* depth_target, std::size_t base_vertex,
                             std::size_t vertex_count,
                             const std::array<const CachedTexture*, 3>& textures,
                             u32 texture_mask);
    const DkImageView* GetOrCreateDepthTarget();
    bool QueueHasError(const char* context);
    void FlushQueue();
#endif
    void DrawSoftwareFallback(std::size_t first_vertex = 0);

    State& state;
    TextureCache& texture_cache;
    ShaderCache& shader_cache;
    SwRenderer::RasterizerSoftware software_fallback;
    std::vector<Pica::OutputVertex> fallback_vertex_batch;
    bool initialized = false;

#ifdef __SWITCH__
    DkDevice device{};
    DkQueue queue{};
    DkMemBlock vertex_mem_block{};
    void* vertex_cpu_buffer = nullptr;
    DkGpuAddr vertex_gpu_addr = 0;
    DkMemBlock uniform_mem_block{};
    void* uniform_cpu_buffer = nullptr;
    DkGpuAddr uniform_gpu_addr = 0;
    DkMemBlock descriptor_mem_block{};
    void* descriptor_cpu_buffer = nullptr;
    DkGpuAddr descriptor_gpu_addr = 0;
    std::deque<DepthTarget> depth_targets;
    DepthTarget* active_depth_target = nullptr;
    std::array<FrameSlice, FrameSliceCount> frame_slices{};
    u32 current_frame_slice = 0;
    mutable std::unordered_set<std::size_t> observed_state_signatures;
    std::unordered_set<std::size_t> blend_signatures;
    DkRasterizerState hw_rasterizer_state{};
    DkMultisampleState hw_multisample_state{};
    DkColorState hw_color_state{};
    DkColorWriteState hw_color_write_state{};
    DkDepthStencilState hw_depth_stencil_state{};
    DkBlendState hw_blend_state{};
    DkImageView hw_color_view{};
    DkImageDescriptor hw_image_descriptor{};
    DkSamplerDescriptor hw_sampler_descriptor{};
#endif
};

} // namespace VideoCore::Deko3D
