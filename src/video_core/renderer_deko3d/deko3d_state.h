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

namespace Memory {
class MemorySystem;
}

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
        bool software_locked = false;
    };

    struct DisplayTransferSurface {
        PAddr display_address = 0;
        DkMemBlock mem_block{};
        DkImage image{};
        DkImageView view{};
        DkMemBlock command_mem_block{};
        DkCmdBuf command_buffer{};
        DkFence fence{};
        u32 width = 0;
        u32 height = 0;
        u32 format = 0;
        u32 output_bytes_per_pixel = 0;
        u64 completed_generation = 0;
        bool fence_pending = false;
        bool valid = false;
    };

    struct PresentImage {
        const DkImageView* view = nullptr;
        u32 width = 0;
        u32 height = 0;
        bool direct_render_target = false;

        [[nodiscard]] bool IsValid() const {
            return view != nullptr && width != 0 && height != 0;
        }
    };

    CachedRenderTarget* GetOrCreateRenderTarget(const RenderTargetKey& key);
    [[nodiscard]] CachedRenderTarget* FindRenderTarget(const RenderTargetKey& key);
    [[nodiscard]] CachedRenderTarget* FindRenderTargetByAddressRange(PAddr address, u32 bytes);
    [[nodiscard]] const CachedRenderTarget* FindGpuDirtyRenderTarget(PAddr address) const;
    [[nodiscard]] PresentImage GetSelectedPresentImage() const;
    [[nodiscard]] PresentImage GetSelectedBottomPresentImage() const;
    void SelectPresentRenderTarget(PAddr address);
    void SelectPresentRenderTargets(PAddr top_address, PAddr bottom_address);
    void MarkRenderTargetGpuDirty(CachedRenderTarget& target);
    bool PrepareRenderTargetForSoftware(CachedRenderTarget& target, Memory::MemorySystem& memory);
    bool PrepareRenderTargetForHardware(CachedRenderTarget& target, Memory::MemorySystem& memory);
    void UnlockSoftwareRenderTarget(PAddr address);
    bool RecordDisplayTransfer(PAddr input_address, PAddr output_address, u32 input_width,
                               u32 input_height, u32 output_width, u32 output_height, u32 flags,
                               u32 output_bytes_per_pixel);
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
    bool CreateSyncResources();
    bool DrawPresentImage(const DkImageView& view, u32 source_width, u32 source_height, u32 slot,
                          u32 scratch_index, u32 src_y, u32 src_height, u32 dst_x, u32 dst_y,
                          u32 dst_width, u32 dst_height, const char* label);
    DisplayTransferSurface* GetOrCreateDisplayTransferSurface(PAddr address, u32 width, u32 height,
                                                              u32 format,
                                                              u32 output_bytes_per_pixel);
    void DestroyDisplayTransferSurface(DisplayTransferSurface& surface);
    bool WaitForSurfaceFence(DisplayTransferSurface& surface);
    bool WaitForSyncFence();
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
    std::vector<std::unique_ptr<DisplayTransferSurface>> display_transfer_surfaces;
    u64 render_target_generation = 0;
    PresentImage selected_present_image{};
    PresentImage selected_bottom_present_image{};
    DkMemBlock sync_mem_block{};
    void* sync_cpu_buffer = nullptr;
    DkGpuAddr sync_gpu_addr = 0;
    u32 sync_buffer_size = 0;
    DkMemBlock sync_command_mem_block{};
    DkCmdBuf sync_command_buffer{};
    DkFence sync_fence{};
    bool sync_fence_pending = false;

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
