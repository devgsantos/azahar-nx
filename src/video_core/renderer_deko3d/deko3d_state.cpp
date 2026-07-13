// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_state.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>

#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "switch/switch_debug_log.h"
#include "video_core/pica/regs_framebuffer.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"

#ifdef __SWITCH__
typedef struct NWindow NWindow;
extern "C" NWindow* nwindowGetDefault(void);
extern "C" bool nwindowIsValid(NWindow* nw);
#endif

namespace VideoCore::Deko3D {
namespace {

#ifdef __SWITCH__
constexpr u32 PresentMemorySize = 4 * 1024;

u64 AlignUp(u64 value, u64 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

std::optional<DkImageFormat> MapRenderTargetColorFormat(Pica::FramebufferRegs::ColorFormat format) {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    switch (format) {
    case ColorFormat::RGBA8:
        return DkImageFormat_RGBA8_Unorm;
    case ColorFormat::RGB5A1:
        return DkImageFormat_RGB5A1_Unorm;
    case ColorFormat::RGB565:
        return DkImageFormat_RGB565_Unorm;
    case ColorFormat::RGBA4:
        return DkImageFormat_RGBA4_Unorm;
    default:
        return std::nullopt;
    }
}

u64 RenderTargetBytes(const State::RenderTargetKey& key) {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    u32 bytes_per_pixel = 4;
    switch (static_cast<ColorFormat>(key.format)) {
    case ColorFormat::RGB8:
        bytes_per_pixel = 3;
        break;
    case ColorFormat::RGB5A1:
    case ColorFormat::RGB565:
    case ColorFormat::RGBA4:
        bytes_per_pixel = 2;
        break;
    case ColorFormat::RGBA8:
    default:
        bytes_per_pixel = 4;
        break;
    }
    return static_cast<u64>(key.width) * key.height * bytes_per_pixel;
}

bool RangesOverlap(PAddr lhs, u64 lhs_size, PAddr rhs, u64 rhs_size) {
    if (lhs_size == 0 || rhs_size == 0) {
        return false;
    }
    const u64 lhs_begin = lhs;
    const u64 lhs_end = lhs_begin + lhs_size;
    const u64 rhs_begin = rhs;
    const u64 rhs_end = rhs_begin + rhs_size;
    return lhs_begin < rhs_end && rhs_begin < lhs_end;
}

struct PresentVertex {
    float position[2];
    float tex_coord[2];
};
#endif

#ifdef __SWITCH__
void Deko3DDebugCallback(void* user_data, const char* context, DkResult result,
                         const char* message) {
    SWITCH_EARLY_LOGF("DEKO3D FATAL context=%s result=%d message=%s",
                      context ? context : "<null>",
                      static_cast<int>(result),
                      message ? message : "<null>");
    LOG_ERROR(Render, "Deko3D validation: context={} result={} message={}",
              context ? context : "", static_cast<int>(result),
              message ? message : "");
}
#endif

} // namespace

State::State() = default;

State::~State() {
    Shutdown();
}

void State::SetError(const char* message) {
    last_error = message ? message : "unknown Deko3D error";
    LOG_ERROR(Render, "Deko3D: {}", last_error);
}

bool State::Initialize() {
#ifndef __SWITCH__
    SetError("Deko3D renderer requested outside Nintendo Switch build");
    return false;
#else
    SWITCH_TRACE_EVENT("Deko3D", "State::Initialize", "enter");
    if (initialized) {
        SWITCH_TRACE_EVENT("Deko3D", "State::Initialize", "already_initialized");
        return true;
    }

    LOG_INFO(Render, "Deko3D renderer initialization started");
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateDevice", "enter");
    if (!CreateDevice()) {
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateDevice", "failed");
        Shutdown();
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateDevice", "Deko3D device created");

    SWITCH_TRACE_EVENT("Deko3D", "State::CreateCommandBuffer", "enter");
    if (!CreateCommandBuffer()) {
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateCommandBuffer", "failed");
        Shutdown();
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateCommandBuffer", "Deko3D command queue buffer created");

    SWITCH_TRACE_EVENT("Deko3D", "State::CreateQueue", "enter");
    if (!CreateQueue()) {
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateQueue", "failed");
        Shutdown();
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateQueue", "Deko3D queue created");

    if (!CreateRasterQueue()) {
        Shutdown();
        return false;
    }

    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "enter");
    if (!CreateFramebuffers()) {
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "failed");
        Shutdown();
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "Deko3D framebuffer created");

    SWITCH_TRACE_EVENT("Deko3D", "State::RecordStaticCommands", "enter");
    if (!RecordStaticCommands()) {
        SWITCH_TRACE_EVENT("Deko3D", "State::RecordStaticCommands", "failed");
        Shutdown();
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::RecordStaticCommands", "leave");

    SWITCH_TRACE_EVENT("Deko3D", "State::CreateScreenTextures", "enter");
    if (!CreateScreenTextures()) {
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateScreenTextures", "failed");
        Shutdown();
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateScreenTextures", "leave");

    // Allocate CPU-accessible screen data buffer for framebuffer uploads
    // Top screen: 400x240 RGBA8, Bottom screen: 320x240 RGBA8
    constexpr u32 TopScreenPixels = 400 * 240;
    constexpr u32 BottomScreenPixels = 320 * 240;
    constexpr u32 BytesPerPixel = 4;
    screen_data_buffer_size = (TopScreenPixels + BottomScreenPixels) * BytesPerPixel;
    screen_data_buffer = new u8[screen_data_buffer_size];

    if (!screen_data_buffer) {
        SetError("Failed to allocate screen data buffer");
        Shutdown();
        return false;
    }

    constexpr u32 TopScreenUploadBytes = 400 * 240 * 4;
    constexpr u32 BottomScreenUploadBytes = 320 * 240 * 4;
    upload_buffer_size = TopScreenUploadBytes + BottomScreenUploadBytes;
    DkMemBlockMaker upload_maker;
    dkMemBlockMakerDefaults(&upload_maker, device,
                            static_cast<u32>(AlignUp(upload_buffer_size, DK_MEMBLOCK_ALIGNMENT)));
    upload_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    upload_mem_block = dkMemBlockCreate(&upload_maker);
    if (!upload_mem_block) {
        SetError("Failed to create upload staging memory block");
        Shutdown();
        return false;
    }
    upload_cpu_buffer = dkMemBlockGetCpuAddr(upload_mem_block);
    upload_gpu_addr = dkMemBlockGetGpuAddr(upload_mem_block);
    if (!upload_cpu_buffer || upload_gpu_addr == 0) {
        SetError("Failed to map upload staging memory block");
        Shutdown();
        return false;
    }

    if (!CreatePresentResources()) {
        Shutdown();
        return false;
    }

    SWITCH_TRACE_EVENTF("Deko3D", "State::Initialize", "screen buffer allocated",
                        "size=%u", screen_data_buffer_size);

    render_targets.reserve(8);
    initialized = true;
    LOG_INFO(Render, "Deko3D renderer initialized: framebuffer={}x{} count={}", FramebufferWidth,
             FramebufferHeight, FramebufferCount);
    SWITCH_TRACE_EVENTF("Deko3D", "State::Initialize", "leave",
                        "framebuffer=%ux%u count=%u", FramebufferWidth, FramebufferHeight,
                        FramebufferCount);
    return true;
#endif
}

void State::Shutdown() {
#ifdef __SWITCH__
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "enter");
    if (queue) {
        SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "wait queue idle enter");
        dkQueueWaitIdle(queue);
        SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "wait queue idle leave");
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy presenter resources enter");
    clear_cmd = 0;
    bind_framebuffer_cmds.fill(0);
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy presenter resources leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy swapchain enter");
    if (swapchain) {
        dkSwapchainDestroy(swapchain);
        swapchain = nullptr;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy swapchain leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy framebuffer image views enter");
    framebuffer_views = {};
    swapchain_images.fill(nullptr);
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy framebuffer image views leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy framebuffer images enter");
    framebuffers = {};
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy framebuffer images leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy framebuffer memory enter");
    if (framebuffer_mem_block) {
        dkMemBlockDestroy(framebuffer_mem_block);
        framebuffer_mem_block = nullptr;
        framebuffer_cpu_buffer = nullptr;
        framebuffer_image_stride = 0;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy framebuffer memory leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy command buffers enter");
    if (cmdbuf) {
        dkCmdBufDestroy(cmdbuf);
        cmdbuf = nullptr;
    }
    if (cmdbuf_mem_block) {
        dkMemBlockDestroy(cmdbuf_mem_block);
        cmdbuf_mem_block = nullptr;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy command buffers leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy upload staging enter");
    if (upload_mem_block) {
        dkMemBlockDestroy(upload_mem_block);
        upload_mem_block = nullptr;
    }
    upload_cpu_buffer = nullptr;
    upload_gpu_addr = 0;
    upload_buffer_size = 0;
    if (present_mem_block) {
        dkMemBlockDestroy(present_mem_block);
        present_mem_block = nullptr;
    }
    present_cpu_buffer = nullptr;
    present_gpu_addr = 0;
    present_sampler = {};
    swapchain_background_initialized = {};
    present_fence = {};
    present_fence_pending = false;
    top_screen_gpu_dirty = false;
    selected_present_render_target = nullptr;
    selected_bottom_present_render_target = nullptr;
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy upload staging leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy render target cache enter");
    display_transfer_targets.clear();
    for (auto& target : render_targets) {
        if (target && target->mem_block) {
            dkMemBlockDestroy(target->mem_block);
            target->mem_block = nullptr;
        }
    }
    render_targets.clear();
    render_targets.reserve(8);
    render_target_generation = 0;
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy render target cache leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy screen textures enter");
    if (screen_tex_mem_block) {
        dkMemBlockDestroy(screen_tex_mem_block);
        screen_tex_mem_block = nullptr;
    }
    if (top_screen_view) {
        delete top_screen_view;
        top_screen_view = nullptr;
    }
    if (bottom_screen_view) {
        delete bottom_screen_view;
        bottom_screen_view = nullptr;
    }
    if (top_screen_image) {
        delete top_screen_image;
        top_screen_image = nullptr;
    }
    if (bottom_screen_image) {
        delete bottom_screen_image;
        bottom_screen_image = nullptr;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy screen textures leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy queue enter");
    if (raster_queue) {
        dkQueueDestroy(raster_queue);
        raster_queue = nullptr;
    }
    if (queue) {
        dkQueueDestroy(queue);
        queue = nullptr;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy queue leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy device-owned resources enter");
    if (device) {
        dkDeviceDestroy(device);
        device = nullptr;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy device-owned resources leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "leave");
#endif
    // Free screen data buffer
    if (screen_data_buffer) {
        delete[] static_cast<u8*>(screen_data_buffer);
        screen_data_buffer = nullptr;
        screen_data_buffer_size = 0;
    }
    initialized = false;
}

void State::WaitIdle() {
#ifdef __SWITCH__
    if (queue) {
        dkQueueWaitIdle(queue);
    }
#endif
}

bool State::PresentClearFrame(float red, float green, float blue, float alpha) {
#ifndef __SWITCH__
    SetError("Deko3D present requested outside Nintendo Switch build");
    return false;
#else
    SWITCH_TRACE_EVENT("Deko3D", "State::PresentClearFrame", "enter");
    if (!initialized || !queue || !swapchain) {
        SetError("Deko3D present requested before initialization");
        SWITCH_TRACE_EVENT("Deko3D", "State::PresentClearFrame", "failed_not_initialized");
        return false;
    }

    const int slot = dkQueueAcquireImage(queue, swapchain);
    if (slot < 0 || slot >= static_cast<int>(FramebufferCount)) {
        SetError("Deko3D failed to acquire swapchain image");
        SWITCH_TRACE_EVENTF("Deko3D", "State::PresentClearFrame", "failed_acquire",
                            "slot=%d", slot);
        return false;
    }
    SWITCH_TRACE_EVENTF("Deko3D", "State::PresentClearFrame", "acquired", "slot=%d", slot);

    dkQueueSubmitCommands(queue, bind_framebuffer_cmds[slot]);

    // Wait for all submitted commands (including the bind above) to finish before reusing
    // the command buffer memory.  Without this, dkCmdBufClear resets the write pointer over
    // the still-in-flight bind_framebuffer_cmds, corrupting the GPU queue and causing
    // dkQueueAcquireImage to crash on the following frame.
    dkQueueWaitIdle(queue);

    // Re-record the clear color so the first playable path can tint frames later from guest state.
    dkCmdBufClear(cmdbuf);
    dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, red, green, blue, alpha);
    clear_cmd = dkCmdBufFinishList(cmdbuf);

    dkQueueSubmitCommands(queue, clear_cmd);
    dkQueuePresentImage(queue, swapchain, slot);
    SWITCH_TRACE_EVENT("Deko3D", "State::PresentClearFrame", "leave");
    return true;
#endif
}

#ifdef __SWITCH__
bool State::CreateDevice() {
    DkDeviceMaker device_maker;
    dkDeviceMakerDefaults(&device_maker);
    device_maker.userData = this;
    device_maker.cbDebug = Deko3DDebugCallback;
    LOG_INFO(Render, "Deko3D debug callback installed");
    device = dkDeviceCreate(&device_maker);
    if (!device) {
        SetError("dkDeviceCreate failed");
        return false;
    }
    LOG_INFO(Render, "Deko3D device created");
    return true;
}

bool State::QueueHasError(const char* context) {
    if (queue && dkQueueIsInErrorState(queue)) {
        RecordQueueError();
        RecordPresentQueueError();
        LOG_ERROR(Render, "Deko3D queue entered error state {}", context ? context : "");
        return true;
    }
    return false;
}

void State::FlushQueue() {
    if (queue) {
        dkQueueFlush(queue);
        RecordQueueFlush();
        RecordPresentQueueFlush();
    }
}

bool State::CreateFramebuffers() {
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "CreateFramebuffers enter");
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "native window obtain enter");
    NWindow* const native_window = nwindowGetDefault();
    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers", "native window pointer",
                        "pointer=%p", static_cast<void*>(native_window));
    if (native_window == nullptr) {
        SetError("nwindowGetDefault returned null");
        return false;
    }
    const bool native_window_valid = nwindowIsValid(native_window);
    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers", "native window valid",
                        "valid=%s", native_window_valid ? "true" : "false");
    if (!native_window_valid) {
        SetError("Default libnx NWindow is invalid");
        return false;
    }

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_Usage2DEngine;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
    layout_maker.dimensions[0] = FramebufferWidth;
    layout_maker.dimensions[1] = FramebufferHeight;

    DkImageLayout framebuffer_layout;
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "image layout initialize enter");
    dkImageLayoutInitialize(&framebuffer_layout, &layout_maker);
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "image layout initialize leave");

    const u64 image_size = dkImageLayoutGetSize(&framebuffer_layout);
    const u32 image_alignment = dkImageLayoutGetAlignment(&framebuffer_layout);
    if (image_size == 0 || image_alignment == 0) {
        SetError("Deko3D image layout returned invalid size or alignment");
        return false;
    }
    const u64 image_stride = AlignUp(image_size, image_alignment);
    const u64 total_framebuffer_allocation = image_stride * FramebufferCount;
    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers", "image size",
                        "size=%llu", static_cast<unsigned long long>(image_size));
    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers", "image alignment",
                        "alignment=%u", image_alignment);
    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers", "image stride",
                        "stride=%llu", static_cast<unsigned long long>(image_stride));
    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers", "framebuffer count",
                        "count=%u", FramebufferCount);
    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers",
                        "total framebuffer allocation", "size=%llu",
                        static_cast<unsigned long long>(total_framebuffer_allocation));
    if (total_framebuffer_allocation > std::numeric_limits<u32>::max()) {
        SetError("Deko3D framebuffer allocation is too large for dkMemBlockCreate");
        return false;
    }

    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device,
                            static_cast<u32>(total_framebuffer_allocation));
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "framebuffer memory create enter");
    framebuffer_mem_block = dkMemBlockCreate(&mem_block_maker);
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "framebuffer memory create leave");
    if (!framebuffer_mem_block) {
        SetError("Deko3D framebuffer memory allocation failed");
        return false;
    }

    framebuffer_cpu_buffer = dkMemBlockGetCpuAddr(framebuffer_mem_block);
    if (!framebuffer_cpu_buffer) {
        // Some Switch/Deko3D paths do not expose CPU mapping for image memory.
        // Keep renderer alive and fall back to GPU clear/present path.
        LOG_WARNING(Render, "Deko3D framebuffer memory CPU mapping unavailable; using clear fallback");
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "framebuffer CPU mapping unavailable");
        framebuffer_image_stride = 0;
    }
    if (framebuffer_cpu_buffer && image_stride > std::numeric_limits<u32>::max()) {
        SetError("Deko3D framebuffer image stride too large");
        return false;
    }
    if (framebuffer_cpu_buffer) {
        framebuffer_image_stride = static_cast<u32>(image_stride);
    }

    for (u32 index = 0; index < FramebufferCount; ++index) {
        const u64 image_offset = image_stride * index;
        const u64 image_range_end = image_offset + image_size;
        SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers", "framebuffer image range",
                            "index=%u offset=%llu end=%llu total=%llu", index,
                            static_cast<unsigned long long>(image_offset),
                            static_cast<unsigned long long>(image_range_end),
                            static_cast<unsigned long long>(total_framebuffer_allocation));
        if ((image_offset % image_alignment) != 0 ||
            image_range_end > total_framebuffer_allocation ||
            image_offset > std::numeric_limits<u32>::max()) {
            SetError("Deko3D framebuffer image offset/range validation failed");
            return false;
        }
        SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers",
                            index == 0 ? "framebuffer[0] initialize enter"
                                       : "framebuffer[1] initialize enter",
                            "offset=%llu", static_cast<unsigned long long>(image_offset));
        dkImageInitialize(&framebuffers[index], &framebuffer_layout, framebuffer_mem_block,
                          static_cast<u32>(image_offset));
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers",
                           index == 0 ? "framebuffer[0] initialize leave"
                                      : "framebuffer[1] initialize leave");
    }

    for (u32 index = 0; index < FramebufferCount; ++index) {
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers",
                           index == 0 ? "framebuffer[0] view initialize enter"
                                      : "framebuffer[1] view initialize enter");
        dkImageViewDefaults(&framebuffer_views[index], &framebuffers[index]);
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers",
                           index == 0 ? "framebuffer[0] view initialize leave"
                                      : "framebuffer[1] view initialize leave");
    }

    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers",
                       "swapchain image array prepare enter");
    for (u32 index = 0; index < FramebufferCount; ++index) {
        swapchain_images[index] = &framebuffers[index];
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers",
                       "swapchain image array prepare leave");

    DkSwapchainMaker swapchain_maker;
    dkSwapchainMakerDefaults(&swapchain_maker, device, native_window, swapchain_images.data(),
                             swapchain_images.size());
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "swapchain create enter");
    swapchain = dkSwapchainCreate(&swapchain_maker);
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "swapchain create leave");
    if (!swapchain) {
        SetError("dkSwapchainCreate failed");
        return false;
    }
    dkSwapchainSetSwapInterval(swapchain, 1);
    LOG_INFO(Render, "Deko3D framebuffer created");
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "CreateFramebuffers leave");
    return true;
}

bool State::CreateCommandBuffer() {
    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device, CommandMemorySize);
    mem_block_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    cmdbuf_mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!cmdbuf_mem_block) {
        SetError("Deko3D command memory allocation failed");
        return false;
    }

    DkCmdBufMaker cmdbuf_maker;
    dkCmdBufMakerDefaults(&cmdbuf_maker, device);
    cmdbuf = dkCmdBufCreate(&cmdbuf_maker);
    if (!cmdbuf) {
        SetError("dkCmdBufCreate failed");
        return false;
    }

    dkCmdBufAddMemory(cmdbuf, cmdbuf_mem_block, 0, CommandMemorySize);
    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateCommandBuffer", "leave", "memory_size=%u",
                        CommandMemorySize);
    return true;
}

bool State::CreateQueue() {
    DkQueueMaker queue_maker;
    dkQueueMakerDefaults(&queue_maker, device);
    queue_maker.flags = DkQueueFlags_Graphics;
    queue = dkQueueCreate(&queue_maker);
    if (!queue) {
        SetError("dkQueueCreate failed");
        return false;
    }
    LOG_INFO(Render, "Deko3D queue created");
    return true;
}

bool State::CreateRasterQueue() {
    // GetRasterQueue() returns the shared presenter queue to avoid two concurrent
    // DkQueueFlags_Graphics queues on the same device, which causes GPU timeouts.
    LOG_INFO(Render, "Deko3D raster queue created");
    return true;
}

bool State::RecordStaticCommands() {
    for (u32 index = 0; index < FramebufferCount; ++index) {
        dkCmdBufClear(cmdbuf);
        dkCmdBufBindRenderTarget(cmdbuf, &framebuffer_views[index], nullptr);
        bind_framebuffer_cmds[index] = dkCmdBufFinishList(cmdbuf);
        if (!bind_framebuffer_cmds[index]) {
            SetError("Deko3D failed to record framebuffer bind command");
            return false;
        }
    }
    return true;
}

bool State::CreateScreenTextures() {
    // Create images for 3DS screens: top (400x240) and bottom (320x240) in RGBA8
    constexpr u32 TopScreenWidth = 400;
    constexpr u32 TopScreenHeight = 240;
    constexpr u32 BottomScreenWidth = 320;
    constexpr u32 BottomScreenHeight = 240;

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags =
        DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_Usage2DEngine;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;

    // Calculate total allocation needed for both screen textures
    // Top screen: 400x240 RGBA8
    layout_maker.dimensions[0] = TopScreenWidth;
    layout_maker.dimensions[1] = TopScreenHeight;
    DkImageLayout top_layout;
    dkImageLayoutInitialize(&top_layout, &layout_maker);
    u64 top_size = dkImageLayoutGetSize(&top_layout);
    u32 top_alignment = dkImageLayoutGetAlignment(&top_layout);

    // Bottom screen: 320x240 RGBA8
    layout_maker.dimensions[0] = BottomScreenWidth;
    layout_maker.dimensions[1] = BottomScreenHeight;
    DkImageLayout bottom_layout;
    dkImageLayoutInitialize(&bottom_layout, &layout_maker);
    u64 bottom_size = dkImageLayoutGetSize(&bottom_layout);
    u32 bottom_alignment = dkImageLayoutGetAlignment(&bottom_layout);

    u64 top_stride = AlignUp(top_size, top_alignment);
    u64 bottom_stride = AlignUp(bottom_size, bottom_alignment);
    u64 total_size = top_stride + bottom_stride;

    if (total_size > std::numeric_limits<u32>::max()) {
        SetError("Screen texture allocation would exceed 32-bit limit");
        return false;
    }

    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateScreenTextures", "allocation",
                        "top_size=%llu bottom_size=%llu total=%llu",
                        (unsigned long long)top_size, (unsigned long long)bottom_size,
                        (unsigned long long)total_size);

    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device, static_cast<u32>(total_size));
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;

    screen_tex_mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!screen_tex_mem_block) {
        SetError("Failed to create screen texture memory block");
        return false;
    }

    // Allocate image objects
    try {
        top_screen_image = new DkImage();
        bottom_screen_image = new DkImage();
        top_screen_view = new DkImageView();
        bottom_screen_view = new DkImageView();
    } catch (const std::exception& e) {
        SetError("Failed to allocate screen image objects");
        return false;
    }

    // Initialize top screen image
    dkImageInitialize(top_screen_image, &top_layout, screen_tex_mem_block, 0);
    dkImageViewDefaults(top_screen_view, top_screen_image);

    // Initialize bottom screen image
    dkImageInitialize(bottom_screen_image, &bottom_layout, screen_tex_mem_block,
                     static_cast<u32>(top_stride));
    dkImageViewDefaults(bottom_screen_view, bottom_screen_image);

    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateScreenTextures", "success",
                        "top=%ux%u bottom=%ux%u", TopScreenWidth, TopScreenHeight,
                        BottomScreenWidth, BottomScreenHeight);
    return true;
}

bool State::CreatePresentResources() {
    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device,
                            static_cast<u32>(AlignUp(PresentMemorySize, DK_MEMBLOCK_ALIGNMENT)));
    mem_block_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    present_mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!present_mem_block) {
        SetError("Failed to create present descriptor/vertex memory block");
        return false;
    }

    present_cpu_buffer = dkMemBlockGetCpuAddr(present_mem_block);
    present_gpu_addr = dkMemBlockGetGpuAddr(present_mem_block);
    if (!present_cpu_buffer || present_gpu_addr == 0) {
        SetError("Failed to map present descriptor/vertex memory block");
        return false;
    }

    dkSamplerDefaults(&present_sampler);
    present_sampler.minFilter = DkFilter_Nearest;
    present_sampler.magFilter = DkFilter_Nearest;
    present_sampler.mipFilter = DkMipFilter_None;
    present_sampler.wrapMode[0] = DkWrapMode_ClampToEdge;
    present_sampler.wrapMode[1] = DkWrapMode_ClampToEdge;
    present_sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
    return true;
}

bool State::DrawCachedScreenRenderTarget(const CachedRenderTarget& target, u32 slot,
                                         u32 scratch_index, u32 src_y, u32 src_height, u32 dst_x,
                                         u32 dst_y, u32 dst_width, u32 dst_height,
                                         const char* label) {
    if (!present_vertex_shader || !present_fragment_shader || !present_cpu_buffer ||
        present_gpu_addr == 0 || slot >= FramebufferCount || src_height == 0 ||
        src_y >= target.key.height) {
        return false;
    }

    constexpr std::size_t PresentScratchStride = 1024;
    const std::size_t scratch_base = static_cast<std::size_t>(scratch_index) * PresentScratchStride;
    constexpr std::size_t ImageOffset = 0;
    const std::size_t sampler_offset =
        AlignUp(ImageOffset + sizeof(DkImageDescriptor), DK_IMAGE_DESCRIPTOR_ALIGNMENT);
    const std::size_t vertex_offset =
        AlignUp(sampler_offset + sizeof(DkSamplerDescriptor), 16);
    const std::size_t required_size =
        scratch_base + vertex_offset + sizeof(PresentVertex) * 4;
    if (required_size > PresentMemorySize) {
        return false;
    }

    DkImageDescriptor image_descriptor{};
    DkSamplerDescriptor sampler_descriptor{};
    dkImageDescriptorInitialize(&image_descriptor, &target.view, false, false);
    dkSamplerDescriptorInitialize(&sampler_descriptor, &present_sampler);
    std::memcpy(static_cast<u8*>(present_cpu_buffer) + scratch_base + ImageOffset, &image_descriptor,
                sizeof(image_descriptor));
    std::memcpy(static_cast<u8*>(present_cpu_buffer) + scratch_base + sampler_offset,
                &sampler_descriptor, sizeof(sampler_descriptor));

    const u32 clamped_src_height = std::min(src_height, target.key.height - src_y);
    const float src_top =
        static_cast<float>(src_y) / static_cast<float>(std::max(target.key.height, 1u));
    const float src_bottom =
        static_cast<float>(src_y + clamped_src_height) /
        static_cast<float>(std::max(target.key.height, 1u));
    const PresentVertex vertices[] = {
        {{-1.0f, +1.0f}, {1.0f, src_top}},
        {{-1.0f, -1.0f}, {0.0f, src_top}},
        {{+1.0f, +1.0f}, {1.0f, src_bottom}},
        {{+1.0f, -1.0f}, {0.0f, src_bottom}},
    };
    std::memcpy(static_cast<u8*>(present_cpu_buffer) + scratch_base + vertex_offset, vertices,
                sizeof(vertices));

    static const DkVtxAttribState attribs[] = {
        {0, 0, offsetof(PresentVertex, position), DkVtxAttribSize_2x32, DkVtxAttribType_Float, 0},
        {0, 0, offsetof(PresentVertex, tex_coord), DkVtxAttribSize_2x32, DkVtxAttribType_Float,
         0},
    };
    static const DkVtxBufferState vtx_buffer_state[] = {{sizeof(PresentVertex), 0}};

    DkRasterizerState rasterizer_state{};
    DkMultisampleState multisample_state{};
    DkColorState color_state{};
    DkColorWriteState color_write_state{};
    DkDepthStencilState depth_stencil_state{};
    DkBlendState blend_state{};
    dkRasterizerStateDefaults(&rasterizer_state);
    dkMultisampleStateDefaults(&multisample_state);
    dkColorStateDefaults(&color_state);
    dkColorWriteStateDefaults(&color_write_state);
    dkDepthStencilStateDefaults(&depth_stencil_state);
    dkBlendStateDefaults(&blend_state);
    rasterizer_state.cullMode = DkFace_None;
    dkColorStateSetBlendEnable(&color_state, 0, false);
    dkColorWriteStateSetMask(&color_write_state, 0, DkColorMask_RGBA);

    const DkViewport viewport = {static_cast<float>(dst_x), static_cast<float>(dst_y),
                                 static_cast<float>(dst_width),
                                 static_cast<float>(dst_height), 0.0f, 1.0f};
    const DkScissor scissor = {dst_x, dst_y, dst_width, dst_height};
    const DkShader* shaders[] = {present_vertex_shader, present_fragment_shader};

    dkCmdBufBindRenderTarget(cmdbuf, &framebuffer_views[slot], nullptr);
    dkCmdBufSetViewports(cmdbuf, 0, &viewport, 1);
    dkCmdBufSetScissors(cmdbuf, 0, &scissor, 1);
    dkCmdBufBindShaders(cmdbuf, DkStageFlag_GraphicsMask, shaders, 2);
    dkCmdBufBarrier(cmdbuf, DkBarrier_Fragments,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
    dkCmdBufBindImageDescriptorSet(cmdbuf, present_gpu_addr + scratch_base + ImageOffset, 1);
    dkCmdBufBindSamplerDescriptorSet(cmdbuf, present_gpu_addr + scratch_base + sampler_offset, 1);
    const DkResHandle texture_handle = dkMakeTextureHandle(0, 0);
    dkCmdBufBindTextures(cmdbuf, DkStage_Fragment, 0, &texture_handle, 1);
    dkCmdBufBindRasterizerState(cmdbuf, &rasterizer_state);
    dkCmdBufBindMultisampleState(cmdbuf, &multisample_state);
    dkCmdBufBindColorState(cmdbuf, &color_state);
    dkCmdBufBindColorWriteState(cmdbuf, &color_write_state);
    dkCmdBufBindBlendState(cmdbuf, 0, &blend_state);
    dkCmdBufBindDepthStencilState(cmdbuf, &depth_stencil_state);
    dkCmdBufBindVtxAttribState(cmdbuf, attribs, sizeof(attribs) / sizeof(attribs[0]));
    dkCmdBufBindVtxBufferState(cmdbuf, vtx_buffer_state,
                               sizeof(vtx_buffer_state) / sizeof(vtx_buffer_state[0]));
    dkCmdBufBindVtxBuffer(cmdbuf, 0, present_gpu_addr + scratch_base + vertex_offset,
                          static_cast<u32>(sizeof(vertices)));
    dkCmdBufDraw(cmdbuf, DkPrimitive_TriangleStrip, 4, 1, 0, 0);
    static auto s_last_present_shader_log_top = std::chrono::steady_clock::time_point{};
    static auto s_last_present_shader_log_bottom = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    auto& last_present_shader_log = label && std::strcmp(label, "bottom") == 0
                                        ? s_last_present_shader_log_bottom
                                        : s_last_present_shader_log_top;
    if (now - last_present_shader_log >= std::chrono::seconds(1)) {
        last_present_shader_log = now;
        LOG_INFO(Render,
                 "Present cached {} shader draw src={}x{} y={} h={} dst={}x{} "
                 "uv={:.3f}..{:.3f}",
                 label ? label : "screen", target.key.width, target.key.height, src_y,
                 clamped_src_height, dst_width, dst_height, src_top, src_bottom);
    }
    return true;
}

State::CachedRenderTarget* State::GetOrCreateRenderTarget(const RenderTargetKey& key) {
    if (!initialized || !device || key.color_address == 0 || key.width == 0 || key.height == 0) {
        return nullptr;
    }

    for (auto& target : render_targets) {
        if (target->key == key) {
            RecordRenderTargetCacheHit();
            return target.get();
        }
    }

    const auto mapped_format =
        MapRenderTargetColorFormat(static_cast<Pica::FramebufferRegs::ColorFormat>(key.format));
    if (!mapped_format) {
        LOG_WARNING(Render,
                    "Deko3D render target cache: unsupported color format {} for addr=0x{:08x}",
                    key.format, key.color_address);
        return nullptr;
    }

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    layout_maker.format = *mapped_format;
    layout_maker.dimensions[0] = key.width;
    layout_maker.dimensions[1] = key.height;

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layout_maker);
    const u64 image_size = dkImageLayoutGetSize(&layout);
    const u32 image_alignment = dkImageLayoutGetAlignment(&layout);
    if (image_size == 0 || image_alignment == 0 ||
        image_size > std::numeric_limits<u32>::max()) {
        return nullptr;
    }

    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device,
                            static_cast<u32>(AlignUp(image_size, image_alignment)));
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    DkMemBlock mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!mem_block) {
        return nullptr;
    }

    auto target = std::make_unique<CachedRenderTarget>();
    target->key = key;
    target->mem_block = mem_block;
    target->allocation_bytes = AlignUp(image_size, image_alignment);
    dkImageInitialize(&target->image, &layout, target->mem_block, 0);
    dkImageViewDefaults(&target->view, &target->image);
    CachedRenderTarget* result = target.get();
    render_targets.emplace_back(std::move(target));
    RecordRenderTargetCacheMiss();
    RecordRenderTargetCacheCreation(result->allocation_bytes);
    LOG_INFO(Render,
             "Deko3D render target cache create: addr=0x{:08x} size={}x{} format={} bytes={}",
             key.color_address, key.width, key.height, key.format,
             static_cast<unsigned long long>(result->allocation_bytes));
    return result;
}

const State::CachedRenderTarget* State::FindGpuDirtyRenderTarget(PAddr address) const {
    for (const auto& target : render_targets) {
        if (target->key.color_address == address && target->gpu_dirty) {
            return target.get();
        }
    }
    for (const auto& alias : display_transfer_targets) {
        const CachedRenderTarget* const target = alias.target;
        if (alias.display_address != address || !target || !target->gpu_dirty ||
            target->owner != SurfaceOwner::Deko3D || alias.deko_generation == 0) {
            continue;
        }
        static auto s_last_alias_lookup_log = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now - s_last_alias_lookup_log >= std::chrono::seconds(1)) {
            s_last_alias_lookup_log = now;
            LOG_INFO(Render,
                     "FindGpuDirtyRenderTarget: display addr=0x{:08x} using transfer RT "
                     "addr=0x{:08x} size={}x{} gen={} alias_gen={} gpu_dirty={}",
                     address, target->key.color_address, target->key.width, target->key.height,
                     target->deko_generation, alias.deko_generation, target->gpu_dirty);
        }
        return target;
    }
    return nullptr;
}

const State::CachedRenderTarget* State::GetSelectedPresentRenderTarget() const {
    return selected_present_render_target;
}

const State::CachedRenderTarget* State::GetSelectedBottomPresentRenderTarget() const {
    return selected_bottom_present_render_target;
}

void State::SelectPresentRenderTarget(PAddr address) {
    SelectPresentRenderTargets(address, 0);
}

void State::SelectPresentRenderTargets(PAddr top_address, PAddr bottom_address) {
    selected_present_render_target = FindGpuDirtyRenderTarget(top_address);
    const auto is_top_shape = [](const CachedRenderTarget* target) {
        return target && target->key.width == 240 && target->key.height >= 400;
    };
    if (!is_top_shape(selected_present_render_target)) {
        selected_present_render_target = nullptr;
    }
    selected_bottom_present_render_target =
        bottom_address != 0 ? FindGpuDirtyRenderTarget(bottom_address) : nullptr;
    if (selected_bottom_present_render_target &&
        (selected_bottom_present_render_target->key.width != 240 ||
         selected_bottom_present_render_target->key.height != 320)) {
        selected_bottom_present_render_target = nullptr;
    }
    if (!render_targets.empty()) {
        static auto s_last_log = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now - s_last_log < std::chrono::seconds(1)) {
            return;
        }
        s_last_log = now;
        LOG_INFO(Render,
                 "SelectPresentRenderTarget: top=0x{:08x} top={}x{} addr=0x{:08x} "
                 "bottom=0x{:08x} bottom={}x{} addr=0x{:08x} rt_count={}",
                 top_address, selected_present_render_target ? selected_present_render_target->key.width : 0,
                 selected_present_render_target ? selected_present_render_target->key.height : 0,
                 selected_present_render_target ? selected_present_render_target->key.color_address : 0,
                 bottom_address,
                 selected_bottom_present_render_target ? selected_bottom_present_render_target->key.width : 0,
                 selected_bottom_present_render_target ? selected_bottom_present_render_target->key.height : 0,
                 selected_bottom_present_render_target ? selected_bottom_present_render_target->key.color_address : 0,
                 render_targets.size());
    }
}

void State::MarkRenderTargetGpuDirty(CachedRenderTarget& target) {
    target.owner = SurfaceOwner::Deko3D;
    target.gpu_dirty = true;
    target.cpu_dirty = false;
    target.deko_generation = ++render_target_generation;
    RecordRenderTargetGpuDirty();
}

bool State::RecordDisplayTransfer(PAddr input_address, PAddr output_address, u32 input_width,
                                  u32 input_height, u32 output_width, u32 output_height,
                                  u32 flags) {
    if (input_address == 0 || output_address == 0) {
        return false;
    }

    const CachedRenderTarget* source = nullptr;
    for (const auto& target : render_targets) {
        if (target->key.color_address == input_address && target->gpu_dirty &&
            target->owner == SurfaceOwner::Deko3D) {
            source = target.get();
            break;
        }
    }
    if (!source) {
        for (const auto& target : render_targets) {
            if (!target->gpu_dirty || target->owner != SurfaceOwner::Deko3D ||
                !RangesOverlap(input_address, 1, target->key.color_address,
                               RenderTargetBytes(target->key))) {
                continue;
            }
            if (!source || target->deko_generation > source->deko_generation) {
                source = target.get();
            }
        }
    }
    if (!source) {
        return false;
    }
    if (source->key.width != input_width || source->key.height != input_height ||
        output_width == 0 || output_height == 0) {
        return false;
    }

    auto existing =
        std::find_if(display_transfer_targets.begin(), display_transfer_targets.end(),
                     [output_address](const auto& entry) {
                         return entry.display_address == output_address;
                     });
    if (existing != display_transfer_targets.end()) {
        existing->target = source;
        existing->deko_generation = source->deko_generation;
        existing->input_width = input_width;
        existing->input_height = input_height;
        existing->output_width = output_width;
        existing->output_height = output_height;
        existing->flags = flags;
    } else {
        display_transfer_targets.emplace_back(DisplayTransferTarget{
            .display_address = output_address,
            .target = source,
            .deko_generation = source->deko_generation,
            .input_width = input_width,
            .input_height = input_height,
            .output_width = output_width,
            .output_height = output_height,
            .flags = flags,
        });
    }
    static auto s_last_transfer_log = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - s_last_transfer_log >= std::chrono::seconds(1)) {
        s_last_transfer_log = now;
        LOG_INFO(Render,
                 "Deko3D display transfer alias: dst=0x{:08x} src=0x{:08x} rt={}x{} "
                 "in={}x{} out={}x{} flags=0x{:08x} gen={}",
                 output_address, source->key.color_address, source->key.width, source->key.height,
                 input_width, input_height, output_width, output_height, flags,
                 source->deko_generation);
    }
    return true;
}

void State::InvalidateRenderTargetsOverlapping(PAddr address, u32 bytes, SurfaceOwner owner) {
    if (address == 0 || bytes == 0) {
        return;
    }
    for (auto& target : render_targets) {
        if (!RangesOverlap(address, bytes, target->key.color_address, RenderTargetBytes(target->key))) {
            continue;
        }
        target->owner = owner;
        target->gpu_dirty = false;
        target->cpu_dirty = true;
        target->guest_memory_generation = ++render_target_generation;
        RecordRenderTargetCpuDirty();
        switch (owner) {
        case SurfaceOwner::CpuMemory:
            RecordRenderTargetCpuDirtyByCpuMemory();
            break;
        case SurfaceOwner::SoftwareRasterizer:
            RecordRenderTargetCpuDirtyBySoftware();
            break;
        case SurfaceOwner::DisplayTransfer:
            RecordRenderTargetCpuDirtyByDisplayTransfer();
            break;
        default:
            break;
        }
        display_transfer_targets.erase(
            std::remove_if(display_transfer_targets.begin(), display_transfer_targets.end(),
                           [raw_target = target.get()](const DisplayTransferTarget& alias) {
                               return alias.target == raw_target;
                           }),
            display_transfer_targets.end());
        if (selected_present_render_target == target.get()) {
            selected_present_render_target = nullptr;
        }
        if (selected_bottom_present_render_target == target.get()) {
            selected_bottom_present_render_target = nullptr;
        }
    }
}

void State::MarkRenderTargetSoftwareDirty(PAddr address, u32 bytes) {
    InvalidateRenderTargetsOverlapping(address, bytes, SurfaceOwner::SoftwareRasterizer);
}

void State::MarkRenderTargetDisplayTransferWrite(PAddr address, u32 bytes) {
    InvalidateRenderTargetsOverlapping(address, bytes, SurfaceOwner::DisplayTransfer);
}

bool State::UploadRenderTargetFromGuest(CachedRenderTarget& target,
                                        const void* guest_pixels,
                                        u32 guest_stride_bytes) {
#ifdef __SWITCH__
    if (!initialized || !device || !queue) {
        return false;
    }
    if (!guest_pixels || target.key.width == 0 || target.key.height == 0) {
        return false;
    }

    const u32 width = target.key.width;
    const u32 height = target.key.height;
    const u32 rgba8_stride = width * 4;
    const u32 rgba8_bytes = rgba8_stride * height;

    // Use the screen_data_buffer as a staging area; it is already CPU-accessible and
    // GPU-mapped. Bail out if the RT is larger than the buffer.
    if (!screen_data_buffer || rgba8_bytes > screen_data_buffer_size) {
        LOG_DEBUG(Render,
                  "UploadRenderTargetFromGuest: RT too large for staging buffer "
                  "{}x{} needs={} have={}",
                  width, height, rgba8_bytes, screen_data_buffer_size);
        return false;
    }

    // Decode guest pixels (any PICA format) into RGBA8 in the staging buffer.
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    const ColorFormat fmt = static_cast<ColorFormat>(target.key.format);
    const u32 bpp = Pica::FramebufferRegs::BytesPerColorPixel(fmt);
    if (bpp == 0) {
        return false;
    }
    const u32 stride_pixels = guest_stride_bytes > 0 ? (guest_stride_bytes / bpp) : width;
    const u8* src = static_cast<const u8*>(guest_pixels);
    u8* dst = static_cast<u8*>(screen_data_buffer);

    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            const u8* px = src + (y * stride_pixels + x) * bpp;
            u8* out = dst + (y * width + x) * 4;
            switch (fmt) {
            case ColorFormat::RGBA8:
                out[0] = px[3]; out[1] = px[2]; out[2] = px[1]; out[3] = px[0];
                break;
            case ColorFormat::RGB8:
                out[0] = px[2]; out[1] = px[1]; out[2] = px[0]; out[3] = 0xff;
                break;
            case ColorFormat::RGB565: {
                const u16 raw = static_cast<u16>(px[0] | (px[1] << 8));
                out[0] = static_cast<u8>(((raw >> 11) & 0x1f) * 255 / 31);
                out[1] = static_cast<u8>(((raw >>  5) & 0x3f) * 255 / 63);
                out[2] = static_cast<u8>(((raw >>  0) & 0x1f) * 255 / 31);
                out[3] = 0xff;
                break;
            }
            case ColorFormat::RGB5A1: {
                const u16 raw = static_cast<u16>(px[0] | (px[1] << 8));
                out[0] = static_cast<u8>(((raw >> 11) & 0x1f) * 255 / 31);
                out[1] = static_cast<u8>(((raw >>  6) & 0x1f) * 255 / 31);
                out[2] = static_cast<u8>(((raw >>  1) & 0x1f) * 255 / 31);
                out[3] = static_cast<u8>((raw & 0x1) * 255);
                break;
            }
            case ColorFormat::RGBA4: {
                const u16 raw = static_cast<u16>(px[0] | (px[1] << 8));
                out[0] = static_cast<u8>(((raw >> 12) & 0xf) * 17);
                out[1] = static_cast<u8>(((raw >>  8) & 0xf) * 17);
                out[2] = static_cast<u8>(((raw >>  4) & 0xf) * 17);
                out[3] = static_cast<u8>(((raw >>  0) & 0xf) * 17);
                break;
            }
            default:
                std::memset(out, 0, 4);
                break;
            }
        }
    }

    // Create a temporary upload command buffer.
    constexpr u32 UploadCmdSize = 4 * 1024;
    DkMemBlockMaker cmd_mem_maker;
    dkMemBlockMakerDefaults(&cmd_mem_maker, device, UploadCmdSize);
    cmd_mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    DkMemBlock cmd_mem = dkMemBlockCreate(&cmd_mem_maker);
    if (!cmd_mem) {
        return false;
    }

    DkCmdBufMaker cmd_buf_maker;
    dkCmdBufMakerDefaults(&cmd_buf_maker, device);
    DkCmdBuf cmd_buf = dkCmdBufCreate(&cmd_buf_maker);
    if (!cmd_buf) {
        dkMemBlockDestroy(cmd_mem);
        return false;
    }
    dkCmdBufAddMemory(cmd_buf, cmd_mem, 0, UploadCmdSize);

    // The screen_data_buffer is a CPU-uncached/GPU-cached block; get its GPU address.
    // We need it for the copy. Re-use the existing upload_gpu_addr if wired, otherwise
    // we need the block's GPU addr — but screen_data_buffer comes from screen_tex_mem_block
    // which is an Image block and may not have a CPU-linear GPU address usable for
    // CopyBufferToImage. Fall back to a dedicated linear staging block.
    DkMemBlockMaker staging_maker;
    dkMemBlockMakerDefaults(&staging_maker, device,
                            AlignUp(static_cast<u64>(rgba8_bytes), DK_MEMBLOCK_ALIGNMENT));
    staging_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    DkMemBlock staging_mem = dkMemBlockCreate(&staging_maker);
    if (!staging_mem) {
        dkCmdBufDestroy(cmd_buf);
        dkMemBlockDestroy(cmd_mem);
        return false;
    }
    void* staging_cpu = dkMemBlockGetCpuAddr(staging_mem);
    DkGpuAddr staging_gpu = dkMemBlockGetGpuAddr(staging_mem);
    if (!staging_cpu || staging_gpu == 0) {
        dkMemBlockDestroy(staging_mem);
        dkCmdBufDestroy(cmd_buf);
        dkMemBlockDestroy(cmd_mem);
        return false;
    }
    std::memcpy(staging_cpu, dst, rgba8_bytes);

    DkCopyBuf copy_buf{};
    copy_buf.addr = staging_gpu;
    copy_buf.rowLength = 0;
    copy_buf.imageHeight = 0;

    DkImageView target_view{};
    dkImageViewDefaults(&target_view, &target.image);

    DkImageRect dst_rect{};
    dst_rect.x = 0; dst_rect.y = 0; dst_rect.z = 0;
    dst_rect.width = width; dst_rect.height = height; dst_rect.depth = 1;

    dkCmdBufCopyBufferToImage(cmd_buf, &copy_buf, &target_view, &dst_rect, 0);
    const DkCmdList cmd_list = dkCmdBufFinishList(cmd_buf);

    bool success = false;
    if (cmd_list) {
        dkQueueFlush(queue);
        dkQueueSubmitCommands(queue, cmd_list);
        dkQueueWaitIdle(queue);
        success = !dkQueueIsInErrorState(queue);
    }

    dkMemBlockDestroy(staging_mem);
    dkCmdBufDestroy(cmd_buf);
    dkMemBlockDestroy(cmd_mem);

    if (success) {
        target.cpu_dirty = false;
        target.gpu_dirty = false;
        target.needs_clear = false;
        LOG_DEBUG(Render, "UploadRenderTargetFromGuest: uploaded {}x{} fmt={} to GPU",
                  width, height, target.key.format);
    }
    return success;
#else
    (void)target; (void)guest_pixels; (void)guest_stride_bytes;
    return false;
#endif
}

void State::UploadScreenTextures() {
    if (!initialized || !top_screen_image || !bottom_screen_image || !screen_data_buffer) {
        return;
    }
}

bool State::PresentScreenTexturesFrame() {
    if (!initialized || !queue || !swapchain) {
        SetError("Deko3D present requested before initialization");
        SWITCH_TRACE_EVENT("Deko3D", "State::PresentScreenTexturesFrame", "failed_not_initialized");
        return false;
    }

    // Upload screen texture data first
    UploadScreenTextures();

    // Wait for the previous present fence BEFORE acquiring, so we never block
    // acquireImage with both swapchain slots still in-flight.
    if (present_fence_pending) {
        if (QueueHasError("before present flush")) {
            return false;
        }
        FlushQueue();
        RecordPresentFencePoll();
        const DkResult poll_result = dkFenceWait(&present_fence, 0);
        if (poll_result == DkResult_Success) {
            RecordFencePollSuccess();
            RecordPresentFencePollSuccess();
            present_fence_pending = false;
        }
    }
    if (present_fence_pending) {
        RecordFenceWait();
        RecordPresentFenceWait();
        constexpr s64 FenceWaitTimeoutNs = 1'000'000'000LL;
        const auto wait_start = std::chrono::steady_clock::now();
        const DkResult result = dkFenceWait(&present_fence, FenceWaitTimeoutNs);
        const auto wait_end = std::chrono::steady_clock::now();
        const auto wait_us =
            std::chrono::duration_cast<std::chrono::microseconds>(wait_end - wait_start).count();
        RecordPresentFenceWaitDurationUs(static_cast<std::uint64_t>(std::max<s64>(wait_us, 0)));
        if (result != DkResult_Success) {
            if (result == DkResult_Timeout) {
                RecordFenceTimeout();
                RecordPresentFenceTimeout();
            }
            SetError("Deko3D present fence wait failed");
            return false;
        }
        present_fence_pending = false;
    }

    const int slot = dkQueueAcquireImage(queue, swapchain);
    if (slot < 0 || slot >= static_cast<int>(FramebufferCount)) {
        SetError("Deko3D failed to acquire swapchain image");
        SWITCH_TRACE_EVENTF("Deko3D", "State::PresentScreenTexturesFrame", "failed_acquire",
                            "slot=%d", slot);
        return false;
    }
    if (!screen_data_buffer || !upload_cpu_buffer || upload_gpu_addr == 0) {
        // Fallback path when the GPU upload path is unavailable.
        dkQueueSubmitCommands(queue, bind_framebuffer_cmds[slot]);
        RecordPresentQueueSubmit();
        if (QueueHasError("after fallback framebuffer bind submit")) {
            return false;
        }
        FlushQueue();
        dkQueueWaitIdle(queue);
        dkCmdBufClear(cmdbuf);
        dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, 0.02f, 0.04f, 0.06f, 1.0f);
        clear_cmd = dkCmdBufFinishList(cmdbuf);
        dkQueueSubmitCommands(queue, clear_cmd);
        RecordPresentQueueSubmit();
        if (QueueHasError("after fallback clear submit")) {
            return false;
        }
        FlushQueue();
        dkQueuePresentImage(queue, swapchain, slot);
        return true;
    }

    u8* const upload_ptr = static_cast<u8*>(upload_cpu_buffer);
    constexpr u32 frame_width = FramebufferWidth;
    constexpr u32 top_width = 400;
    constexpr u32 top_height = 240;
    constexpr u32 bottom_width = 320;
    constexpr u32 bottom_height = 240;
    constexpr u32 top_bytes = top_width * top_height * 4;
    constexpr u32 bottom_bytes = bottom_width * bottom_height * 4;

    const u8* const top_src = static_cast<const u8*>(screen_data_buffer);
    const u8* const bottom_src = top_src + top_bytes;
    const CachedRenderTarget* const cached_present = selected_present_render_target;
    const CachedRenderTarget* const cached_bottom_present = selected_bottom_present_render_target;
    if (!cached_present) {
        std::memcpy(upload_ptr, top_src, top_bytes);
    }
    if (!cached_bottom_present) {
        std::memcpy(upload_ptr + top_bytes, bottom_src, bottom_bytes);
    }

    const u32 top_x = (frame_width - top_width) / 2;
    const u32 top_y = 40;
    const u32 bottom_x = (frame_width - bottom_width) / 2;
    const u32 bottom_y = 320;

    dkCmdBufClear(cmdbuf);

    if (!swapchain_background_initialized[slot]) {
        dkCmdBufBindRenderTarget(cmdbuf, &framebuffer_views[slot], nullptr);
        dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, 0.125f, 0.188f, 0.376f, 1.0f);
        swapchain_background_initialized[slot] = true;
    }

    // Place top and bottom 3DS screens centered on Switch output.  Hardware-rasterized guest
    // render targets stay on the GPU and are blitted directly; CPU upload remains the fallback.
    DkImageRect top_copy_dst = {top_x, top_y, 0, top_width, top_height, 1};
    if (cached_present) {
        const u32 top_src_y = 0;
        const u32 top_src_height = std::min(400u, cached_present->key.height - top_src_y);
        WaitRasterQueue();
        if (!DrawCachedScreenRenderTarget(*cached_present, static_cast<u32>(slot), 0, top_src_y,
                                          top_src_height, top_x, top_y, top_width, top_height,
                                          "top")) {
            const u32 rt_w = cached_present->key.width;
            DkImageRect rt_src = {0, top_src_y, 0, rt_w, top_src_height, 1};
            dkCmdBufBarrier(cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
            dkCmdBufBlitImage(cmdbuf, &cached_present->view, &rt_src,
                              &framebuffer_views[slot], &top_copy_dst,
                              DkBlitFlag_FilterNearest | DkBlitFlag_ModeBlit, 0);
            LOG_INFO(Render, "Present cached top shader unavailable; blit fallback src={}x{} "
                             "dst={}x{}",
                     rt_w, top_src_height, top_copy_dst.width, top_copy_dst.height);
        }
    } else if (top_screen_gpu_dirty && top_screen_view) {
        DkImageRect top_copy_src = {0, 0, 0, top_width, top_height, 1};
        dkCmdBufBarrier(cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
        dkCmdBufBlitImage(cmdbuf, top_screen_view, &top_copy_src, &framebuffer_views[slot],
                          &top_copy_dst, DkBlitFlag_FilterNearest | DkBlitFlag_ModeBlit, 0);
    } else {
        DkCopyBuf top_copy_src = {upload_gpu_addr, 0, 0};
        dkCmdBufCopyBufferToImage(cmdbuf, &top_copy_src, &framebuffer_views[slot], &top_copy_dst,
                                  0);
    }

    bool bottom_from_rt = false;
    DkImageRect bottom_copy_dst = {bottom_x, bottom_y, 0, bottom_width, bottom_height, 1};
    if (cached_bottom_present && cached_bottom_present->key.width == 240 &&
        cached_bottom_present->key.height == 320) {
        WaitRasterQueue();
        if (DrawCachedScreenRenderTarget(*cached_bottom_present, static_cast<u32>(slot), 1, 0,
                                         320, bottom_x, bottom_y, bottom_width, bottom_height,
                                         "bottom")) {
            bottom_from_rt = true;
        } else {
            DkImageRect bottom_rt_src = {0, 0, 0, cached_bottom_present->key.width,
                                         std::min(cached_bottom_present->key.height, 320u), 1};
            dkCmdBufBarrier(cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
            dkCmdBufBlitImage(cmdbuf, &cached_bottom_present->view, &bottom_rt_src,
                              &framebuffer_views[slot], &bottom_copy_dst,
                              DkBlitFlag_FilterNearest | DkBlitFlag_ModeBlit, 0);
            bottom_from_rt = true;
            LOG_INFO(Render, "Present cached bottom shader unavailable; blit fallback src={}x{} "
                             "dst={}x{}",
                     bottom_rt_src.width, bottom_rt_src.height, bottom_copy_dst.width,
                     bottom_copy_dst.height);
        }
    }
    if (!bottom_from_rt) {
        DkCopyBuf bottom_copy_src = {upload_gpu_addr + top_bytes, 0, 0};
        dkCmdBufCopyBufferToImage(cmdbuf, &bottom_copy_src, &framebuffer_views[slot],
                                  &bottom_copy_dst, 0);
    }

    dkCmdBufSignalFence(cmdbuf, &present_fence, false);
    const DkCmdList copy_cmd = dkCmdBufFinishList(cmdbuf);
    if (!copy_cmd) {
        SetError("Deko3D failed to record buffer-to-image copy command");
        return false;
    }

    dkQueueSubmitCommands(queue, copy_cmd);
    RecordPresentQueueSubmit();
    if (QueueHasError("after present copy submit")) {
        return false;
    }
    FlushQueue();
    dkQueueWaitIdle(queue);
    if (QueueHasError("after present flush")) {
        return false;
    }
    present_fence_pending = true;
    dkQueuePresentImage(queue, swapchain, slot);
    if (QueueHasError("after present image")) {
        return false;
    }
    return true;
}
#endif

} // namespace VideoCore::Deko3D
