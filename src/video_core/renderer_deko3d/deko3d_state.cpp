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

    SWITCH_TRACE_EVENT("Deko3D", "State::CreateScreenTextures", "enter");
    if (!CreateScreenTextures()) {
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateScreenTextures", "failed");
        Shutdown();
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateScreenTextures", "leave");

    SWITCH_TRACE_EVENT("Deko3D", "State::CreateShaderPipeline", "enter");
    if (!CreateShaderPipeline()) {
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateShaderPipeline", "failed");
        Shutdown();
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateShaderPipeline", "leave");

    SWITCH_TRACE_EVENT("Deko3D", "State::CreateVertexBuffer", "enter");
    if (!CreateVertexBuffer()) {
        SWITCH_TRACE_EVENT("Deko3D", "State::CreateVertexBuffer", "failed");
        Shutdown();
        return false;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::CreateVertexBuffer", "leave");

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
    SWITCH_TRACE_EVENTF("Deko3D", "State::Initialize", "screen buffer allocated",
                        "size=%u", screen_data_buffer_size);

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
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy GPU buffer enter");
    if (screen_data_gpu_mem_block) {
        dkMemBlockDestroy(screen_data_gpu_mem_block);
        screen_data_gpu_mem_block = nullptr;
        screen_data_gpu_buffer = nullptr;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy GPU buffer leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy shaders enter");
    if (vertex_shader) {
        delete vertex_shader;
        vertex_shader = nullptr;
    }
    if (fragment_shader) {
        delete fragment_shader;
        fragment_shader = nullptr;
    }
    if (shader_mem_block) {
        dkMemBlockDestroy(shader_mem_block);
        shader_mem_block = nullptr;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy shaders leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy vertex buffer enter");
    if (vertex_buffer_mem_block) {
        dkMemBlockDestroy(vertex_buffer_mem_block);
        vertex_buffer_mem_block = nullptr;
        vertex_buffer = nullptr;
        vertex_buffer_size = 0;
    }
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy vertex buffer leave");
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

bool State::CreateScreenTextures() {
    // Create images for 3DS screens: top (400x240) and bottom (320x240) in RGBA8
    constexpr u32 TopScreenWidth = 400;
    constexpr u32 TopScreenHeight = 240;
    constexpr u32 BottomScreenWidth = 320;
    constexpr u32 BottomScreenHeight = 240;

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent;
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

    // Allocate GPU-accessible buffer for pixel uploads (same size as CPU buffer)
    constexpr u32 TopScreenPixels = 400 * 240;
    constexpr u32 BottomScreenPixels = 320 * 240;
    constexpr u32 BytesPerPixel = 4;
    u32 gpu_buffer_size = (TopScreenPixels + BottomScreenPixels) * BytesPerPixel;

    DkMemBlockMaker gpu_mem_maker;
    dkMemBlockMakerDefaults(&gpu_mem_maker, device, gpu_buffer_size);
    gpu_mem_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_CpuUncached;

    screen_data_gpu_mem_block = dkMemBlockCreate(&gpu_mem_maker);
    if (!screen_data_gpu_mem_block) {
        SetError("Failed to create GPU screen data buffer");
        return false;
    }

    screen_data_gpu_buffer = dkMemBlockGetCpuAddr(screen_data_gpu_mem_block);
    if (!screen_data_gpu_buffer) {
        SetError("Failed to get GPU buffer CPU address");
        return false;
    }

    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateScreenTextures", "GPU buffer allocated",
                        "size=%u", gpu_buffer_size);
    return true;
}

void State::UploadScreenTextures() {
    if (!initialized || !screen_data_gpu_buffer || !top_screen_image || !bottom_screen_image) {
        return;
    }

    try {
        // Copy CPU buffer to GPU buffer
        if (screen_data_buffer && screen_data_buffer_size > 0) {
            std::memcpy(screen_data_gpu_buffer, screen_data_buffer, screen_data_buffer_size);
        }

        // Use GPU commands to copy buffer to images
        // Top screen: 400x240 at offset 0
        DkImageRect top_rect{};
        top_rect.x = 0;
        top_rect.y = 0;
        top_rect.width = 400;
        top_rect.height = 240;

        // Bottom screen: 320x240 at offset (400*240*4)
        DkImageRect bottom_rect{};
        bottom_rect.x = 0;
        bottom_rect.y = 0;
        bottom_rect.width = 320;
        bottom_rect.height = 240;

        SWITCH_TRACE_EVENTF("Deko3D", "State::UploadScreenTextures", "uploading",
                            "top=%ux%u bottom=%ux%u",
                            top_rect.width, top_rect.height,
                            bottom_rect.width, bottom_rect.height);
    } catch (const std::exception& e) {
        LOG_WARNING(Render, "Deko3D screen texture upload error: {}", e.what());
    }
}

bool State::PresentScreenTexturesFrame() {
    SWITCH_TRACE_EVENT("Deko3D", "State::PresentScreenTexturesFrame", "enter");
    if (!initialized || !queue || !swapchain) {
        SetError("Deko3D present requested before initialization");
        SWITCH_TRACE_EVENT("Deko3D", "State::PresentScreenTexturesFrame", "failed_not_initialized");
        return false;
    }

    // Upload screen texture data first
    UploadScreenTextures();

    const int slot = dkQueueAcquireImage(queue, swapchain);
    if (slot < 0 || slot >= static_cast<int>(FramebufferCount)) {
        SetError("Deko3D failed to acquire swapchain image");
        SWITCH_TRACE_EVENTF("Deko3D", "State::PresentScreenTexturesFrame", "failed_acquire",
                            "slot=%d", slot);
        return false;
    }
    SWITCH_TRACE_EVENTF("Deko3D", "State::PresentScreenTexturesFrame", "acquired", "slot=%d", slot);

    // Bind framebuffer
    dkQueueSubmitCommands(queue, bind_framebuffer_cmds[slot]);

    // Wait for commands to complete
    dkQueueWaitIdle(queue);

    // Record rendering commands
    dkCmdBufClear(cmdbuf);
    dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);

    // Bind screen texture views for sampling
    // Top screen texture (400x240) bound to descriptor set
    if (top_screen_view) {
        SWITCH_TRACE_EVENT("Deko3D", "State::PresentScreenTexturesFrame", "binding top screen");
        // TODO: Bind top_screen_view as sampled texture in descriptor
    }

    // Bottom screen texture (320x240) bound to descriptor set
    if (bottom_screen_view) {
        SWITCH_TRACE_EVENT("Deko3D", "State::PresentScreenTexturesFrame", "binding bottom screen");
        // TODO: Bind bottom_screen_view as sampled texture in descriptor
    }

    // Bind and draw fullscreen quad
    if (vertex_buffer && vertex_buffer_size > 0) {
        SWITCH_TRACE_EVENTF("Deko3D", "State::PresentScreenTexturesFrame", "rendering quad",
                            "vb_size=%u", vertex_buffer_size);

        // Bind vertex buffer to GPU memory
        DkGpuAddr vb_addr = dkMemBlockGetGpuAddr(vertex_buffer_mem_block);
        dkCmdBufBindVtxBuffer(cmdbuf, 0, vb_addr, vertex_buffer_size);

        // Bind shaders (when implemented)
        if (vertex_shader && fragment_shader) {
            // TODO: Bind vertex and fragment shaders to pipeline
        }

        // Draw fullscreen quad (2 triangles = 6 vertices, but we only have 4)
        // Will draw as 4-vertex strip
        dkCmdBufDraw(cmdbuf, DkPrimitive_TriangleStrip, 4, 1, 0, 0);

        SWITCH_TRACE_EVENT("Deko3D", "State::PresentScreenTexturesFrame", "quad draw submitted");
    } else {
        // Fallback: Just render a clear with indicator color
        LOG_WARNING(Render, "Vertex buffer not available, rendering indicator only");
        dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, 0.2f, 0.1f, 0.05f, 1.0f);
    }

    clear_cmd = dkCmdBufFinishList(cmdbuf);

    dkQueueSubmitCommands(queue, clear_cmd);
    dkQueuePresentImage(queue, swapchain, slot);
    SWITCH_TRACE_EVENT("Deko3D", "State::PresentScreenTexturesFrame", "leave");
    return true;
}

bool State::CreateShaderPipeline() {
    // Simple GLSL shaders compiled offline to SPIR-V for Switch
    // For now, allocate minimal shader memory - actual shader bytecode would be loaded here
    constexpr u32 ShaderMemSize = 4096; // Placeholder for shader code

    DkMemBlockMaker shader_mem_maker;
    dkMemBlockMakerDefaults(&shader_mem_maker, device, ShaderMemSize);
    shader_mem_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_CpuUncached;

    shader_mem_block = dkMemBlockCreate(&shader_mem_maker);
    if (!shader_mem_block) {
        SetError("Failed to create shader memory block");
        return false;
    }

    // Allocate shader objects
    try {
        vertex_shader = new DkShader();
        fragment_shader = new DkShader();
    } catch (const std::exception& e) {
        SetError("Failed to allocate shader objects");
        return false;
    }

    // TODO: Load actual compiled GLSL shaders here
    // For now, shaders are placeholder - they would contain:
    // - Vertex shader: Transform fullscreen quad with rotation, pass UV coords
    // - Fragment shader: Sample from screen textures and output color
    // - Both would handle layout composition (top 400x240 + bottom 320x240 on 1280x720)

    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateShaderPipeline", "shaders allocated",
                        "mem_size=%u", ShaderMemSize);
    return true;
}

bool State::CreateVertexBuffer() {
    // Fullscreen quad: 4 vertices covering entire 1280x720 framebuffer
    // Each vertex: position (2 floats) + UV (2 floats) = 4 floats = 16 bytes
    // Total: 4 * 16 = 64 bytes
    struct Vertex {
        float x, y;      // Position
        float u, v;      // Texture coordinate
    };

    constexpr u32 NumVertices = 4;
    vertex_buffer_size = NumVertices * sizeof(Vertex);

    DkMemBlockMaker vertex_mem_maker;
    dkMemBlockMakerDefaults(&vertex_mem_maker, device, vertex_buffer_size);
    vertex_mem_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_CpuUncached;

    vertex_buffer_mem_block = dkMemBlockCreate(&vertex_mem_maker);
    if (!vertex_buffer_mem_block) {
        SetError("Failed to create vertex buffer memory block");
        return false;
    }

    vertex_buffer = dkMemBlockGetCpuAddr(vertex_buffer_mem_block);
    if (!vertex_buffer) {
        SetError("Failed to get vertex buffer CPU address");
        return false;
    }

    // Fill vertex buffer with fullscreen quad
    // The quad spans the entire 1280x720 framebuffer
    // With 90-degree rotation applied in shader for landscape display
    Vertex* verts = static_cast<Vertex*>(vertex_buffer);

    // Triangle 1: top-left, top-right, bottom-left
    verts[0] = {-1.0f, 1.0f, 0.0f, 0.0f};    // top-left (top screen origin)
    verts[1] = {1.0f, 1.0f, 1.0f, 0.0f};     // top-right
    verts[2] = {-1.0f, -1.0f, 0.0f, 1.0f};   // bottom-left

    // Triangle 2: top-right, bottom-right, bottom-left
    verts[3] = {1.0f, -1.0f, 1.0f, 1.0f};    // bottom-right

    SWITCH_TRACE_EVENTF("Deko3D", "State::CreateVertexBuffer", "buffer created",
                        "size=%u vertices=%u", vertex_buffer_size, NumVertices);
    return true;
}
#endif

} // namespace VideoCore::Deko3D
