// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include "common/common_types.h"

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

    // Accessors for screen textures (used by Presenter for CPU framebuffer upload)
    [[nodiscard]] void* GetScreenDataBuffer() const {
        return screen_data_buffer;
    }

    [[nodiscard]] u32 GetScreenDataSize() const {
        return screen_data_buffer_size;
    }

    // Upload pixel data from CPU buffer to GPU textures
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
        return queue; // share the single presenter queue — two Graphics queues on same device fault
    }

    void WaitRasterQueue() {
        // Raster and presentation use the same graphics queue. A flush makes submitted work visible,
        // while queue ordering guarantees that the following present commands execute afterwards.
        // Waiting for the whole queue here serialized every screen blit with every hardware draw.
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

    // Screen textures for CPU framebuffer display (400x240 top, 320x240 bottom)
    DkMemBlock screen_tex_mem_block{};
    DkImage* top_screen_image = nullptr;     // 400x240 RGBA8
    DkImage* bottom_screen_image = nullptr;  // 320x240 RGBA8
    DkImageView* top_screen_view = nullptr;
    DkImageView* bottom_screen_view = nullptr;

    // Screen texture data buffer for CPU framebuffer upload
    void* screen_data_buffer = nullptr;
    u32 screen_data_buffer_size = 0;
#endif

    bool initialized = false;
    std::string last_error;
};

#ifdef __SWITCH__
namespace PerfSync {

// The renderer intentionally uses one graphics queue. These small wrappers keep ordering intact
// while removing redundant whole-queue drains from hot paths. They are scoped by source filename so
// shutdown, error recovery, and unrelated Deko3D users retain the original blocking semantics.
inline thread_local bool present_fence_recorded = false;
inline thread_local bool present_submission_pending = false;
inline thread_local bool texture_completion_wait = false;
inline thread_local u32 descriptor_slot = 0;

constexpr u32 DescriptorSlotCount = 3;
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
    if (SourceIs(file, "deko3d_state.cpp") && present_fence_recorded) {
        present_fence_recorded = false;
        present_submission_pending = true;
    }
}

inline void QueueWaitIdle(DkQueue wait_queue, const char* file, int line) {
    if (SourceIs(file, "deko3d_state.cpp")) {
        // The hot present wait is near the end of State::PresentScreenTexturesFrame. Its fence is
        // consumed before the next acquire, so this extra queue-wide wait only destroys overlap.
        if (present_submission_pending && line >= 1280) {
            present_submission_pending = false;
            return;
        }
        // Any shutdown/fallback wait must remain real and also clears stale hot-path state.
        present_submission_pending = false;
    } else if (SourceIs(file, "deko3d_texture_cache.cpp")) {
        // Texture uploads use one staging allocation. Keep the post-submit completion wait, but
        // remove the redundant pre-submit drain; the previous upload already completed before reuse.
        if (!texture_completion_wait) {
            texture_completion_wait = true;
            return;
        }
        texture_completion_wait = false;
    }
    ::dkQueueWaitIdle(wait_queue);
}

} // namespace PerfSync
#endif

} // namespace VideoCore::Deko3D

#ifdef __SWITCH__
#define dkCmdBufSignalFence(command_buffer, fence, flush)                                         \
    ::VideoCore::Deko3D::PerfSync::CmdBufSignalFence((command_buffer), (fence), (flush), __FILE__)
#define dkQueueSubmitCommands(submit_queue, command_list)                                         \
    ::VideoCore::Deko3D::PerfSync::QueueSubmitCommands((submit_queue), (command_list), __FILE__)
#define dkQueueWaitIdle(wait_queue)                                                                \
    ::VideoCore::Deko3D::PerfSync::QueueWaitIdle((wait_queue), __FILE__, __LINE__)
#endif
