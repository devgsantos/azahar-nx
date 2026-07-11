// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <cstddef>
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

#ifdef __SWITCH__
namespace AsyncRaster {

// SubmitHardwareChunk already owns a three-entry command/vertex/uniform ring. Main waited for every
// fence immediately after submission, making that ring ineffective. A negative timeout is the one
// immediate completion wait in that path; defer it and let WaitForFrameSlice synchronize only when
// the corresponding ring entry is reused.
inline thread_local bool defer_next_fence_clear = false;

inline DkResult FenceWait(DkFence* fence, s64 timeout) {
    if (timeout < 0) {
        defer_next_fence_clear = true;
        return DkResult_Success;
    }
    return ::dkFenceWait(fence, timeout);
}

class DeferredFencePending {
public:
    DeferredFencePending() = default;
    DeferredFencePending(bool value) : pending{value} {}

    DeferredFencePending& operator=(bool value) {
        if (!value && defer_next_fence_clear) {
            pending = true;
            defer_next_fence_clear = false;
        } else {
            pending = value;
        }
        return *this;
    }

    operator bool() const {
        return pending;
    }

private:
    bool pending = false;
};

// Descriptor memory was the only per-draw resource not divided into ring slots. Keep descriptors in
// lockstep with command submissions so asynchronous draws never observe data from a later draw.
class DescriptorCpuBuffer {
public:
    DescriptorCpuBuffer() = default;

    DescriptorCpuBuffer& operator=(void* address) {
        base = static_cast<u8*>(address);
        PerfSync::ResetDescriptorSlot();
        return *this;
    }

    explicit operator bool() const {
        return base != nullptr;
    }

    operator u8*() const {
        return base ? base + PerfSync::DescriptorOffset() : nullptr;
    }

private:
    u8* base = nullptr;
};

class DescriptorGpuAddress {
public:
    DescriptorGpuAddress() = default;

    DescriptorGpuAddress& operator=(DkGpuAddr address) {
        base = address;
        PerfSync::ResetDescriptorSlot();
        return *this;
    }

    operator DkGpuAddr() const {
        return base == 0 ? 0 : base + PerfSync::DescriptorOffset();
    }

    bool operator==(DkGpuAddr address) const {
        return base == address;
    }

    bool operator!=(DkGpuAddr address) const {
        return base != address;
    }

    bool operator==(int address) const {
        return base == static_cast<DkGpuAddr>(address);
    }

    bool operator!=(int address) const {
        return base != static_cast<DkGpuAddr>(address);
    }

    friend DkGpuAddr operator+(const DescriptorGpuAddress& address, std::size_t offset) {
        return address.base == 0 ? 0
                                 : address.base + PerfSync::DescriptorOffset() +
                                       static_cast<DkGpuAddr>(offset);
    }

private:
    DkGpuAddr base = 0;
};

template <typename T>
class DiagnosticsSet {
public:
#if defined(AZAHAR_SWITCH_PERF_DIAGNOSTICS)
    auto insert(const T& value) {
        return values.insert(value);
    }
#else
    struct InsertResult {
        bool second = false;
    };

    InsertResult insert(const T&) const {
        return {};
    }
#endif

private:
#if defined(AZAHAR_SWITCH_PERF_DIAGNOSTICS)
    std::unordered_set<T> values;
#endif
};

} // namespace AsyncRaster
#endif

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
        DkFence fence{};
        AsyncRaster::DeferredFencePending fence_pending{};
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
    AsyncRaster::DescriptorCpuBuffer descriptor_cpu_buffer{};
    AsyncRaster::DescriptorGpuAddress descriptor_gpu_addr{};
    std::deque<DepthTarget> depth_targets;
    DepthTarget* active_depth_target = nullptr;
    std::array<FrameSlice, FrameSliceCount> frame_slices{};
    u32 current_frame_slice = 0;
    mutable AsyncRaster::DiagnosticsSet<std::size_t> observed_state_signatures;
    AsyncRaster::DiagnosticsSet<std::size_t> blend_signatures;
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

#ifdef __SWITCH__
#define dkFenceWait(fence, timeout)                                                               \
    ::VideoCore::Deko3D::AsyncRaster::FenceWait((fence), (timeout))

#if !defined(AZAHAR_SWITCH_PERF_DIAGNOSTICS)
#define RecordHardwareDrawSubmitted(...) ((void)0)
#define RecordHardwareDrawCompleted(...) ((void)0)
#define RecordHardwareDrawAttempt(...) ((void)0)
#define RecordHardwareDrawFailure(...) ((void)0)
#define RecordSoftwareFallback(...) ((void)0)
#define RecordRingWait(...) ((void)0)
#define RecordFencePollSuccess(...) ((void)0)
#define RecordFenceWait(...) ((void)0)
#define RecordFenceWaitDurationMs(...) ((void)0)
#define RecordFenceTimeout(...) ((void)0)
#define RecordQueueError(...) ((void)0)
#define RecordQueueFlush(...) ((void)0)
#define RecordFallbackReason(...) ((void)0)
#define RecordRasterQueueSubmit(...) ((void)0)
#define RecordRasterQueueFlush(...) ((void)0)
#define RecordRasterFencePoll(...) ((void)0)
#define RecordRasterFencePollSuccess(...) ((void)0)
#define RecordRasterFenceWait(...) ((void)0)
#define RecordRasterFenceTimeout(...) ((void)0)
#define RecordRasterFenceWaitDurationUs(...) ((void)0)
#define RecordRasterQueueError(...) ((void)0)
#define RecordTransformedBatchCheck(...) ((void)0)
#define RecordTransformedBatchEligible(...) ((void)0)
#define RecordTransformedBatchSubmitted(...) ((void)0)
#define RecordTransformedBatchCompleted(...) ((void)0)
#define RecordDirectBatchRejected(...) ((void)0)
#define RecordFallbackInvalidTransformedBatch(...) ((void)0)
#define RecordBlocker(...) ((void)0)
#define RecordTransformedBlocker(...) ((void)0)
#define RecordDirectBlocker(...) ((void)0)
#define RecordRenderTargetCacheHit(...) ((void)0)
#define RecordRenderTargetCacheMiss(...) ((void)0)
#define RecordRenderTargetCacheCreation(...) ((void)0)
#define RecordRenderTargetCacheEviction(...) ((void)0)
#define RecordRenderTargetGpuDirty(...) ((void)0)
#define RecordRenderTargetCpuDirty(...) ((void)0)
#define RecordBlendState(...) ((void)0)
#define RecordDepthState(...) ((void)0)
#define RecordStateSignature(...) ((void)0)
#define RecordPartialBatch(...) ((void)0)
#define RecordDuplicateTrianglePrevention(...) ((void)0)
#define RecordDroppedTriangleDetection(...) ((void)0)
#define RecordHardwareRasterFrame(...) ((void)0)
#define RecordSoftwareRasterFrame(...) ((void)0)
#define RecordTransferOnlyFrame(...) ((void)0)
#endif
#endif
