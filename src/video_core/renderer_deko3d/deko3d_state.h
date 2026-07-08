// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <deque>
#include <string>

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
    [[nodiscard]] DkDevice GetDevice() const {
        return device;
    }

    [[nodiscard]] DkQueue GetQueue() const {
        return queue;
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

        CachedRenderTarget() = default;
        CachedRenderTarget(const CachedRenderTarget& other)
            : key{other.key}, mem_block{other.mem_block}, image{other.image},
              allocation_bytes{other.allocation_bytes}, owner{other.owner},
              guest_memory_generation{other.guest_memory_generation},
              deko_generation{other.deko_generation},
              software_raster_generation{other.software_raster_generation},
              display_transfer_generation{other.display_transfer_generation},
              last_presented_generation{other.last_presented_generation},
              gpu_dirty{other.gpu_dirty}, cpu_dirty{other.cpu_dirty} {
            if (mem_block) {
                dkImageViewDefaults(&view, &image);
            }
        }
        CachedRenderTarget& operator=(const CachedRenderTarget&) = delete;
    };

    CachedRenderTarget* GetOrCreateRenderTarget(const RenderTargetKey& key);
    [[nodiscard]] const CachedRenderTarget* FindGpuDirtyRenderTarget(PAddr address) const;
    [[nodiscard]] const CachedRenderTarget* GetSelectedPresentRenderTarget() const;
    [[nodiscard]] const CachedRenderTarget* GetSelectedBottomPresentRenderTarget() const;
    void SelectPresentRenderTarget(PAddr address);
    void SelectBottomPresentRenderTarget(PAddr address);
    void MarkRenderTargetGpuDirty(CachedRenderTarget& target);
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
    bool RecordStaticCommands();
    bool CreateScreenTextures();
    bool QueueHasError(const char* context);
    void FlushQueue();

    DkDevice device{};
    DkQueue queue{};
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
    std::deque<CachedRenderTarget> render_targets;
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

} // namespace VideoCore::Deko3D
