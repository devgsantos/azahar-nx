// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "common/common_types.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"

#ifdef __SWITCH__
#include <deko3d.h>
#endif

namespace VideoCore::Deko3D {

class State {
public:
    static constexpr u32 FramebufferCount = 2;
    static constexpr u32 FramebufferWidth = 1280;
    static constexpr u32 FramebufferHeight = 720;
    static constexpr u32 CommandMemorySize = 64 * 1024;

    State();
    ~State();

    bool Initialize();
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const {
        return initialized;
    }

    [[nodiscard]] const std::string& LastError() const {
        return last_error;
    }

    bool PresentClearFrame(float red, float green, float blue, float alpha);
    bool PresentScreenTexturesFrame();
    void WaitIdle();

    [[nodiscard]] void* GetScreenDataBuffer() const {
        return screen_data_buffer;
    }

    [[nodiscard]] u32 GetScreenDataSize() const {
        return screen_data_buffer_size;
    }

    void UploadScreenTextures();

    [[nodiscard]] const DkImage* GetTopScreenImage() const {
        return top_screen_image;
    }

    [[nodiscard]] const DkImage* GetBottomScreenImage() const {
        return bottom_screen_image;
    }

#ifdef __SWITCH__
    void SetPresentShaders(const DkShader* vertex_shader, const DkShader* fragment_shader) {
        present_vertex_shader = vertex_shader;
        present_fragment_shader = fragment_shader;
    }

    [[nodiscard]] DkDevice GetDevice() const {
        return device;
    }

    [[nodiscard]] DkQueue GetQueue() const {
        return queue;
    }

    [[nodiscard]] DkQueue GetRasterQueue() const {
        return queue;
    }

    void WaitRasterQueue() {
        if (queue) {
            dkQueueFlush(queue);
        }
    }

    [[nodiscard]] const DkImageView* GetTopScreenRenderTargetView() const {
        return top_screen_view;
    }

    struct RenderTargetKey {
        PAddr color_address = 0;
        u32 width = 0;
        u32 height = 0;
        u32 format = 0;

        bool operator==(const RenderTargetKey& other) const {
            return color_address == other.color_address && width == other.width &&
                   height == other.height && format == other.format;
        }
    };

    enum class SurfaceOwner {
        Clean,
        CpuMemory,
        SoftwareRasterizer,
        Deko3D,
        DisplayTransfer,
    };

    struct CachedRenderTarget {
        RenderTargetKey key{};
        DkMemBlock mem_block{};
        DkImage image{};
        DkImageView view{};
        u64 allocation_bytes = 0;
        SurfaceOwner owner = SurfaceOwner::Clean;
        u64 guest_memory_generation = 0;
        u64 deko_generation = 0;
        u64 software_raster_generation = 0;
        u64 display_transfer_generation = 0;
        u64 last_presented_generation = 0;
        bool gpu_dirty = false;
        bool cpu_dirty = false;
        bool needs_clear = true;
    };

    struct DisplayTransferTarget {
        PAddr display_address = 0;
        const CachedRenderTarget* target = nullptr;
        u64 deko_generation = 0;
        u32 input_width = 0;
        u32 input_height = 0;
        u32 output_width = 0;
        u32 output_height = 0;
        u32 flags = 0;
    };

    CachedRenderTarget* GetOrCreateRenderTarget(const RenderTargetKey& key);
    [[nodiscard]] const CachedRenderTarget* FindGpuDirtyRenderTarget(PAddr address) const;
    [[nodiscard]] const CachedRenderTarget* GetSelectedPresentRenderTarget() const;
    [[nodiscard]] const CachedRenderTarget* GetSelectedBottomPresentRenderTarget() const;
    void SelectPresentRenderTarget(PAddr address);
    void SelectPresentRenderTargets(PAddr top_address, PAddr bottom_address);
    void MarkRenderTargetGpuDirty(CachedRenderTarget& target);
    bool RecordDisplayTransfer(PAddr input_address, PAddr output_address, u32 input_width,
                               u32 input_height, u32 output_width, u32 output_height, u32 flags);
    void MarkRenderTargetSoftwareDirty(PAddr address, u32 bytes);
    void MarkRenderTargetDisplayTransferWrite(PAddr address, u32 bytes);
    void InvalidateRenderTargetsOverlapping(PAddr address, u32 bytes, SurfaceOwner owner);

    void MarkTopScreenGpuDirty() {
        top_screen_gpu_dirty = true;
    }

    [[nodiscard]] bool IsTopScreenGpuDirty() const {
        return top_screen_gpu_dirty;
    }
#endif

private:
    void SetError(const char* message);

#ifdef __SWITCH__
    bool CreateDevice();
    bool CreateFramebuffers();
    bool CreateCommandBuffer();
    bool CreateQueue();
    bool CreateRasterQueue();
    bool RecordStaticCommands();
    bool CreateScreenTextures();
    bool CreatePresentResources();
    bool DrawCachedScreenRenderTarget(const CachedRenderTarget& target, u32 slot,
                                      u32 scratch_index, u32 src_y, u32 src_height, u32 dst_x,
                                      u32 dst_y, u32 dst_width, u32 dst_height,
                                      const char* label);
    bool QueueHasError(const char* context);
    void FlushQueue();

    DkDevice device{};
    DkQueue queue{};
    DkQueue raster_queue{};
    DkMemBlock framebuffer_mem_block{};
    std::array<DkImage, FramebufferCount> framebuffers{};
    std::array<DkImageView, FramebufferCount> framebuffer_views{};
    std::array<const DkImage*, FramebufferCount> swapchain_images{};
    void* framebuffer_cpu_buffer = nullptr;
    u32 framebuffer_image_stride = 0;
    DkSwapchain swapchain{};
    DkMemBlock cmdbuf_mem_block{};
    DkCmdBuf cmdbuf{};
    std::array<DkCmdList, FramebufferCount> bind_framebuffer_cmds{};
    DkCmdList clear_cmd{};

    DkMemBlock upload_mem_block{};
    void* upload_cpu_buffer = nullptr;
    DkGpuAddr upload_gpu_addr = 0;
    u32 upload_buffer_size = 0;
    DkMemBlock present_mem_block{};
    void* present_cpu_buffer = nullptr;
    DkGpuAddr present_gpu_addr = 0;
    DkSampler present_sampler{};
    const DkShader* present_vertex_shader = nullptr;
    const DkShader* present_fragment_shader = nullptr;
    std::array<bool, FramebufferCount> swapchain_background_initialized{};
    DkFence present_fence{};
    bool present_fence_pending = false;
    bool top_screen_gpu_dirty = false;
    std::vector<std::unique_ptr<CachedRenderTarget>> render_targets;
    std::vector<DisplayTransferTarget> display_transfer_targets;
    u64 render_target_generation = 0;
    const CachedRenderTarget* selected_present_render_target = nullptr;
    const CachedRenderTarget* selected_bottom_present_render_target = nullptr;

    DkMemBlock screen_tex_mem_block{};
    DkImage* top_screen_image = nullptr;
    DkImage* bottom_screen_image = nullptr;
    DkImageView* top_screen_view = nullptr;
    DkImageView* bottom_screen_view = nullptr;

    void* screen_data_buffer = nullptr;
    u32 screen_data_buffer_size = 0;
#endif

    bool initialized = false;
    std::string last_error;
};

#ifdef __SWITCH__
namespace PerfSync {

inline thread_local bool present_fence_recorded = false;
inline thread_local bool present_submission_pending = false;
inline thread_local u32 descriptor_slot = 0;
inline thread_local DkQueue texture_queue{};

constexpr u32 DescriptorSlotCount = 8;
constexpr u32 DescriptorSlotSize = 16 * 1024;

inline bool SourceIs(const char* file, const char* name) {
    return file != nullptr && std::strstr(file, name) != nullptr;
}

inline u32 DescriptorOffset() {
    return descriptor_slot * DescriptorSlotSize;
}

inline void ResetDescriptorSlot() {
    descriptor_slot = 0;
}

inline void AdvanceDescriptorSlot() {
    descriptor_slot = (descriptor_slot + 1) % DescriptorSlotCount;
}

inline void CmdBufSignalFence(DkCmdBuf command_buffer, DkFence* fence, bool flush,
                              const char* file) {
    ::dkCmdBufSignalFence(command_buffer, fence, flush);
    if (SourceIs(file, "deko3d_state.cpp")) {
        present_fence_recorded = true;
    }
}

inline void QueueSubmitCommands(DkQueue submit_queue, DkCmdList command_list,
                                const char* file) {
    ::dkQueueSubmitCommands(submit_queue, command_list);
    if (SourceIs(file, "deko3d_rasterizer.cpp")) {
        AdvanceDescriptorSlot();
    }
    if (SourceIs(file, "deko3d_texture_cache.cpp")) {
        texture_queue = submit_queue;
    }
    if (SourceIs(file, "deko3d_state.cpp") && present_fence_recorded) {
        present_fence_recorded = false;
        present_submission_pending = true;
    }
}

inline void QueueWaitIdle(DkQueue wait_queue, const char* file, int line) {
    if (SourceIs(file, "deko3d_state.cpp")) {
        if (present_submission_pending && line >= 1280) {
            present_submission_pending = false;
            return;
        }
        present_submission_pending = false;
    }
    ::dkQueueWaitIdle(wait_queue);
}

inline void MemBlockDestroy(DkMemBlock mem_block, const char* file) {
    // Texture cache entries can be evicted or invalidated while draw command lists still reference
    // their image views. Drain only at destruction boundaries; steady-state cache hits/uploads remain
    // asynchronous and ordered on the shared graphics queue.
    if (SourceIs(file, "deko3d_texture_cache.cpp") && texture_queue) {
        ::dkQueueFlush(texture_queue);
        ::dkQueueWaitIdle(texture_queue);
    }
    ::dkMemBlockDestroy(mem_block);
}

} // namespace PerfSync
#endif

} // namespace VideoCore::Deko3D

#if defined(__SWITCH__) && !defined(AZAHAR_SWITCH_PERF_DIAGNOSTICS)
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
#define RecordPresentQueueSubmit(...) ((void)0)
#define RecordPresentQueueFlush(...) ((void)0)
#define RecordPresentFencePoll(...) ((void)0)
#define RecordPresentFencePollSuccess(...) ((void)0)
#define RecordPresentFenceWait(...) ((void)0)
#define RecordPresentFenceTimeout(...) ((void)0)
#define RecordPresentFenceWaitDurationUs(...) ((void)0)
#define RecordPresentQueueError(...) ((void)0)
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
#define RecordPicaCommandList(...) ((void)0)
#define RecordPicaDraw(...) ((void)0)
#define RecordPicaOutputTriangles(...) ((void)0)
#define RecordPicaMemoryFill(...) ((void)0)
#define RecordPicaDisplayTransfer(...) ((void)0)
#define RecordPicaTextureCopy(...) ((void)0)
#define RecordPicaCacheFlush(...) ((void)0)
#define RecordPicaCacheInvalidation(...) ((void)0)
#define RecordGspInterruptRequested(...) ((void)0)
#define RecordGspInterruptDelivered(...) ((void)0)
#define RecordGspInterruptDropped(...) ((void)0)
#define RecordGspLogicalInterruptRaised(...) ((void)0)
#define RecordGspThreadDeliveryAttempt(...) ((void)0)
#define RecordGspInterruptIgnoredNoActiveThread(...) ((void)0)
#define RecordGspInterruptIgnoredUnregisteredThread(...) ((void)0)
#define RecordGspInterruptIgnoredNoEvent(...) ((void)0)
#define RecordGspInterruptQueueFull(...) ((void)0)
#define RecordGspInterruptStaleScheduledEvent(...) ((void)0)
#define RecordGspInterruptActualDropped(...) ((void)0)
#define RecordFramebufferChange(...) ((void)0)
#define RecordPresent(...) ((void)0)
#define RecordSystemFrame(...) ((void)0)
#define RecordGameFrame(...) ((void)0)
#define RecordHardwareRasterFrame(...) ((void)0)
#define RecordSoftwareRasterFrame(...) ((void)0)
#define RecordTransferOnlyFrame(...) ((void)0)
#endif

#ifdef __SWITCH__
#define dkCmdBufSignalFence(command_buffer, fence, flush)                                         \
    ::VideoCore::Deko3D::PerfSync::CmdBufSignalFence((command_buffer), (fence), (flush), __FILE__)
#define dkQueueSubmitCommands(submit_queue, command_list)                                         \
    ::VideoCore::Deko3D::PerfSync::QueueSubmitCommands((submit_queue), (command_list), __FILE__)
#define dkQueueWaitIdle(wait_queue)                                                                \
    ::VideoCore::Deko3D::PerfSync::QueueWaitIdle((wait_queue), __FILE__, __LINE__)
#define dkMemBlockDestroy(mem_block)                                                               \
    ::VideoCore::Deko3D::PerfSync::MemBlockDestroy((mem_block), __FILE__)
#endif
