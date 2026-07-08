// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <cstddef>
#include <memory>
#include <vector>

#include "video_core/pica/output_vertex.h"
#include "video_core/pica/regs_external.h"
#include "video_core/rasterizer_accelerated.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"

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
class TextureCache;

class Rasterizer final : public VideoCore::RasterizerAccelerated {
public:
    Rasterizer(Memory::MemorySystem& memory, Pica::PicaCore& pica, State& state,
               TextureCache& texture_cache, ShaderCache& shader_cache);
    ~Rasterizer() override;

    bool Initialize();
    void Shutdown();

    void AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                     const Pica::OutputVertex& v2) override;
    void DrawTriangles() override;
    void FlushAll() override;
    void FlushRegion(PAddr addr, u32 size) override;
    void InvalidateRegion(PAddr addr, u32 size) override;
    void FlushAndInvalidateRegion(PAddr addr, u32 size) override;
    void ClearAll(bool flush) override;

    bool AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) override;
    bool AccelerateTextureCopy(const Pica::DisplayTransferConfig& config) override;
    bool AccelerateFill(const Pica::MemoryFillConfig& config) override;
    bool AccelerateDrawBatch(bool is_indexed) override;

private:
#ifdef __SWITCH__
    static constexpr u32 FrameContextCount = 32;
    static constexpr u32 TransferUploadContextCount = 8;
    static constexpr u32 RasterCommandMemorySize = 512 * 1024;
    static constexpr u32 VertexBufferSize = 4 * 1024 * 1024;
    static constexpr u32 UniformBufferSize = 2 * 1024 * 1024;
    static constexpr u32 FlushBatchSize = 16;

    struct FrameContext {
        u32 command_offset = 0;
        u32 command_size = 0;
        u32 vertex_offset = 0;
        u32 vertex_size = 0;
        u32 uniform_offset = 0;
        u32 uniform_size = 0;
        DkFence fence{};
        bool fence_pending = false;
        std::size_t pending_vertices = 0;
    };

    struct DepthKey {
        PAddr address = 0;
        u32 width = 0;
        u32 height = 0;
        Pica::FramebufferRegs::DepthFormat format{};

        bool operator==(const DepthKey& rhs) const {
            return address == rhs.address && width == rhs.width && height == rhs.height &&
                   format == rhs.format;
        }
    };

    struct DepthSurface {
        DepthKey key{};
        DkMemBlock mem{};
        DkImage image{};
        DkImageView view{};
    };

    struct TransferSourceKey {
        PAddr address = 0;
        u32 width = 0;
        u32 height = 0;
        Pica::PixelFormat format{};
        bool linear = false;

        bool operator==(const TransferSourceKey& rhs) const {
            return address == rhs.address && width == rhs.width && height == rhs.height &&
                   format == rhs.format && linear == rhs.linear;
        }
    };

    struct TransferSource {
        TransferSourceKey key{};
        DkMemBlock mem{};
        DkImage image{};
        DkImageView view{};
    };

    struct TransferUploadContext {
        DkMemBlock mem{};
        void* cpu = nullptr;
        DkGpuAddr gpu = DK_GPU_ADDR_INVALID;
        u32 capacity = 0;
        DkFence fence{};
        bool fence_pending = false;
    };

    struct alignas(16) TevPackedStage {
        u32 sources = 0;
        u32 modifiers = 0;
        u32 operations = 0;
        u32 scales = 0;
    };

    struct alignas(16) FragmentUniforms {
        std::array<TevPackedStage, 6> stages{};
        std::array<std::array<float, 4>, 6> constants{};
        std::array<float, 4> combiner_buffer{};
        u32 update_mask_rgb = 0;
        u32 update_mask_alpha = 0;
        u32 texture_enable_mask = 0;
        u32 alpha_test = 0;
        float alpha_reference = 0.0f;
        std::array<float, 3> padding{};
    };

    bool InitializeGpuResources();
    void ShutdownGpuResources();
    FrameContext& CurrentFrameContext();
    bool WaitForFrameContext(FrameContext& context);
    bool SubmitHardwareBatch();
    bool SubmitHardwareChunk(FrameContext& context, State::CachedRenderTarget& color_target,
                             const DkImageView* depth_target, std::size_t base_vertex,
                             std::size_t vertex_count,
                             const std::array<DkResHandle, 3>& texture_handles,
                             const FragmentUniforms& fragment_uniforms);
    DepthSurface* GetOrCreateDepthSurface();
    FragmentUniforms BuildFragmentUniforms(u32 texture_enable_mask) const;
    bool QueueHasError(const char* context);
    void FlushQueue();
    void FlushQueueIfNeeded(bool force);
    bool IsNativeBatchValid() const;

    TransferSource* GetOrCreateTransferSource(const Pica::DisplayTransferConfig& config);
    TransferUploadContext* AcquireTransferUploadContext(u32 required_bytes);
    bool DecodeTransferSource(const Pica::DisplayTransferConfig& config, void* destination,
                              u32 destination_size) const;
    bool SubmitGpuSurfaceTransfer(const DkImageView& source, u32 source_width, u32 source_height,
                                  State::CachedRenderTarget& destination, u32 flags);
    bool SubmitGuestMemoryTransfer(const Pica::DisplayTransferConfig& config,
                                   TransferSource& source,
                                   State::CachedRenderTarget& destination, u32 flags);
    static u32 CanonicalColorFormat(Pica::PixelFormat format);
#endif

    State& state;
    TextureCache& texture_cache;
    ShaderCache& shader_cache;
    bool initialized = false;

#ifdef __SWITCH__
    DkDevice device{};
    DkQueue queue{};
    DkMemBlock command_mem_block{};
    DkCmdBuf command_buffer{};
    DkMemBlock vertex_mem_block{};
    void* vertex_cpu_buffer = nullptr;
    DkGpuAddr vertex_gpu_addr = DK_GPU_ADDR_INVALID;
    DkMemBlock uniform_mem_block{};
    void* uniform_cpu_buffer = nullptr;
    DkGpuAddr uniform_gpu_addr = DK_GPU_ADDR_INVALID;
    std::array<FrameContext, FrameContextCount> frame_contexts{};
    u32 current_frame_context = 0;
    u32 submissions_since_flush = 0;
    std::vector<std::unique_ptr<DepthSurface>> depth_surfaces;
    std::vector<std::unique_ptr<TransferSource>> transfer_sources;
    std::array<TransferUploadContext, TransferUploadContextCount> transfer_upload_contexts{};
    u32 current_transfer_upload_context = 0;
#endif
};

} // namespace VideoCore::Deko3D
