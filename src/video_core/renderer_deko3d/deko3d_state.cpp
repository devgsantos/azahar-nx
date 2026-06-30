// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_state.h"

#include <limits>

#include "common/logging/log.h"
#include "common/switch_trace.h"

#ifdef __SWITCH__
typedef struct NWindow NWindow;
extern "C" NWindow* nwindowGetDefault(void);
extern "C" bool nwindowIsValid(NWindow* nw);
#endif

namespace VideoCore::Deko3D {
namespace {

#ifdef __SWITCH__
u64 AlignUp(u64 value, u64 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
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
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy queue enter");
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
    device = dkDeviceCreate(&device_maker);
    if (!device) {
        SetError("dkDeviceCreate failed");
        return false;
    }
    LOG_INFO(Render, "Deko3D device created");
    return true;
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
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent;
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
#endif

} // namespace VideoCore::Deko3D
