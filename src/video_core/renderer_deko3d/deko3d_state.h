// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
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
        if (queue) {
            dkQueueFlush(queue);
            dkQueueWaitIdle(queue);
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

    CachedRenderTarget* GetOrCreateRenderTarget(const RenderTargetKey& key);
    [[nodiscard]] const CachedRenderTarget* FindGpuDirtyRenderTarget(PAddr address) const;
    [[nodiscard]] const CachedRenderTarget* GetSelectedPresentRenderTarget() const;
    [[nodiscard]] const CachedRenderTarget* GetSelectedBottomPresentRenderTarget() const;
    void SelectPresentRenderTarget(PAddr address);
    void SelectPresentRenderTargets(PAddr top_address, PAddr bottom_address);
    void MarkRenderTargetGpuDirty(CachedRenderTarget& target);
    void RecordDisplayTransfer(PAddr input_address, PAddr output_address);
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
    std::array<bool, FramebufferCount> swapchain_background_initialized{};
    DkFence present_fence{};
    bool present_fence_pending = false;
    bool top_screen_gpu_dirty = false;
    std::vector<std::unique_ptr<CachedRenderTarget>> render_targets;
    std::vector<std::pair<PAddr, const CachedRenderTarget*>> display_transfer_targets;
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

} // namespace VideoCore::Deko3D
