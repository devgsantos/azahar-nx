// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_state.h"

#include "common/logging/log.h"
#include "common/switch_trace.h"

#ifdef __SWITCH__
typedef struct NWindow NWindow;
extern "C" NWindow* nwindowGetDefault(void);
#endif

namespace VideoCore::Deko3D {

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
    if (queue) {
        dkQueueWaitIdle(queue);
    }
    if (queue) {
        dkQueueDestroy(queue);
        queue = nullptr;
    }
    if (cmdbuf) {
        dkCmdBufDestroy(cmdbuf);
        cmdbuf = nullptr;
    }
    if (cmdbuf_mem_block) {
        dkMemBlockDestroy(cmdbuf_mem_block);
        cmdbuf_mem_block = nullptr;
    }
    if (swapchain) {
        dkSwapchainDestroy(swapchain);
        swapchain = nullptr;
    }
    if (framebuffer_mem_block) {
        dkMemBlockDestroy(framebuffer_mem_block);
        framebuffer_mem_block = nullptr;
    }
    if (device) {
        dkDeviceDestroy(device);
        device = nullptr;
    }
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
    if (!nwindowGetDefault()) {
        SetError("nwindowGetDefault failed");
        return false;
    }

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.flags =
        DkImageFlags_UsageRender | DkImageFlags_UsagePresent | DkImageFlags_HwCompression;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
    layout_maker.dimensions[0] = FramebufferWidth;
    layout_maker.dimensions[1] = FramebufferHeight;

    DkImageLayout framebuffer_layout;
    dkImageLayoutInitialize(&framebuffer_layout, &layout_maker);

    const u32 framebuffer_align = dkImageLayoutGetAlignment(&framebuffer_layout);
    u32 framebuffer_size = dkImageLayoutGetSize(&framebuffer_layout);
    framebuffer_size = (framebuffer_size + framebuffer_align - 1) & ~(framebuffer_align - 1);
    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateFramebuffers", "layout",
                        "width=%u height=%u align=%u size=%u count=%u", FramebufferWidth,
                        FramebufferHeight, framebuffer_align, framebuffer_size, FramebufferCount);

    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device, FramebufferCount * framebuffer_size);
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    framebuffer_mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!framebuffer_mem_block) {
        SetError("Deko3D framebuffer memory allocation failed");
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "memory_created");

    std::array<const DkImage*, FramebufferCount> swapchain_images{};
    for (u32 index = 0; index < FramebufferCount; ++index) {
        swapchain_images[index] = &framebuffers[index];
        dkImageInitialize(&framebuffers[index], &framebuffer_layout, framebuffer_mem_block,
                          index * framebuffer_size);
    }

    DkSwapchainMaker swapchain_maker;
    dkSwapchainMakerDefaults(&swapchain_maker, device, nwindowGetDefault(), swapchain_images.data(),
                             swapchain_images.size());
    swapchain = dkSwapchainCreate(&swapchain_maker);
    if (!swapchain) {
        SetError("dkSwapchainCreate failed");
        return false;
    }
    dkSwapchainSetSwapInterval(swapchain, 1);
    LOG_INFO(Render, "Deko3D framebuffer created");
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateFramebuffers", "swapchain_created");
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
        DkImageView image_view;
        dkImageViewDefaults(&image_view, &framebuffers[index]);
        dkCmdBufBindRenderTarget(cmdbuf, &image_view, nullptr);
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
