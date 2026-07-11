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

#include "common/color.h"
#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "core/memory.h"
#include "switch/switch_debug_log.h"
#include "video_core/pica/regs_external.h"
#include "video_core/pica/regs_framebuffer.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"
#include "video_core/utils.h"

#ifdef __SWITCH__
typedef struct NWindow NWindow;
extern "C" NWindow* nwindowGetDefault(void);
extern "C" bool nwindowIsValid(NWindow* nw);
#endif

namespace VideoCore::Deko3D {
namespace {

#ifdef __SWITCH__
constexpr u32 PresentMemorySize = 4 * 1024;
constexpr u32 DisplayTransferCommandMemorySize = 16 * 1024;
constexpr u32 RenderTargetSyncCommandMemorySize = 16 * 1024;
constexpr u32 RenderTargetSyncBufferSize = 2 * 1024 * 1024;

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

u64 DisplaySurfaceBytes(const State::DisplayTransferSurface& surface) {
    const u32 bytes_per_pixel = surface.output_bytes_per_pixel != 0 ? surface.output_bytes_per_pixel : 4;
    return static_cast<u64>(surface.width) * surface.height * bytes_per_pixel;
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

const char* SurfaceOwnerName(State::SurfaceOwner owner) {
    switch (owner) {
    case State::SurfaceOwner::Clean:
        return "Clean";
    case State::SurfaceOwner::Deko3D:
        return "Deko3D";
    case State::SurfaceOwner::CpuMemory:
        return "CpuMemory";
    case State::SurfaceOwner::SoftwareRasterizer:
        return "SoftwareRasterizer";
    case State::SurfaceOwner::DisplayTransfer:
        return "DisplayTransfer";
    }
    return "Unknown";
}

struct PresentVertex {
    float position[2];
    float tex_coord[2];
};

u32 GuestOffset(u32 x, u32 y, u32 width, u32 bytes_per_pixel) {
    const u32 coarse_y = y & ~7;
    return VideoCore::GetMortonOffset(x, y, bytes_per_pixel) + coarse_y * width * bytes_per_pixel;
}

Common::Vec4<u8> DecodeGuestColor(const u8* src, Pica::FramebufferRegs::ColorFormat format) {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    switch (format) {
    case ColorFormat::RGBA8:
        return Common::Color::DecodeRGBA8(src);
    case ColorFormat::RGB8:
        return Common::Color::DecodeRGB8(src);
    case ColorFormat::RGB5A1:
        return Common::Color::DecodeRGB5A1(src);
    case ColorFormat::RGB565:
        return Common::Color::DecodeRGB565(src);
    case ColorFormat::RGBA4:
        return Common::Color::DecodeRGBA4(src);
    default:
        return {0, 0, 0, 0};
    }
}

void EncodeGuestColor(const Common::Vec4<u8>& color, u8* dst,
                      Pica::FramebufferRegs::ColorFormat format) {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    switch (format) {
    case ColorFormat::RGBA8:
        Common::Color::EncodeRGBA8(color, dst);
        break;
    case ColorFormat::RGB8:
        Common::Color::EncodeRGB8(color, dst);
        break;
    case ColorFormat::RGB5A1:
        Common::Color::EncodeRGB5A1(color, dst);
        break;
    case ColorFormat::RGB565:
        Common::Color::EncodeRGB565(color, dst);
        break;
    case ColorFormat::RGBA4:
        Common::Color::EncodeRGBA4(color, dst);
        break;
    default:
        break;
    }
}

Common::Vec4<u8> DecodeDekoColor(const u8* src, Pica::FramebufferRegs::ColorFormat format) {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    switch (format) {
    case ColorFormat::RGBA8:
        return {src[0], src[1], src[2], src[3]};
    case ColorFormat::RGB8:
        return {src[0], src[1], src[2], 255};
    case ColorFormat::RGB5A1:
        return Common::Color::DecodeRGB5A1(src);
    case ColorFormat::RGB565:
        return Common::Color::DecodeRGB565(src);
    case ColorFormat::RGBA4:
        return Common::Color::DecodeRGBA4(src);
    default:
        return {0, 0, 0, 0};
    }
}

void EncodeDekoColor(const Common::Vec4<u8>& color, u8* dst,
                     Pica::FramebufferRegs::ColorFormat format) {
    using ColorFormat = Pica::FramebufferRegs::ColorFormat;
    switch (format) {
    case ColorFormat::RGBA8:
        dst[0] = color.r();
        dst[1] = color.g();
        dst[2] = color.b();
        dst[3] = color.a();
        break;
    case ColorFormat::RGB8:
        dst[0] = color.r();
        dst[1] = color.g();
        dst[2] = color.b();
        break;
    case ColorFormat::RGB5A1:
        Common::Color::EncodeRGB5A1(color, dst);
        break;
    case ColorFormat::RGB565:
        Common::Color::EncodeRGB565(color, dst);
        break;
    case ColorFormat::RGBA4:
        Common::Color::EncodeRGBA4(color, dst);
        break;
    default:
        break;
    }
}
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
    if (!CreateSyncResources()) {
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
    if (sync_command_buffer) {
        dkCmdBufDestroy(sync_command_buffer);
        sync_command_buffer = nullptr;
    }
    if (sync_command_mem_block) {
        dkMemBlockDestroy(sync_command_mem_block);
        sync_command_mem_block = nullptr;
    }
    if (sync_mem_block) {
        dkMemBlockDestroy(sync_mem_block);
        sync_mem_block = nullptr;
    }
    sync_cpu_buffer = nullptr;
    sync_gpu_addr = 0;
    sync_buffer_size = 0;
    sync_fence = {};
    sync_fence_pending = false;
    swapchain_background_initialized = {};
    present_fence = {};
    present_fence_pending = false;
    top_screen_gpu_dirty = false;
    selected_present_image = {};
    selected_bottom_present_image = {};
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy upload staging leave");
    SWITCH_TRACE_EVENT("Deko3D", "State::Shutdown", "destroy render target cache enter");
    for (auto& surface : display_transfer_surfaces) {
        if (surface) {
            DestroyDisplayTransferSurface(*surface);
        }
    }
    display_transfer_surfaces.clear();
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
    const u64 image_stride = AlignUp(AlignUp(image_size, image_alignment), DK_MEMBLOCK_ALIGNMENT);
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

    u64 top_stride = AlignUp(AlignUp(top_size, top_alignment), bottom_alignment);
    u64 bottom_stride = AlignUp(bottom_size, bottom_alignment);
    u64 total_size = AlignUp(top_stride + bottom_stride, DK_MEMBLOCK_ALIGNMENT);

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

bool State::CreateSyncResources() {
    DkMemBlockMaker sync_maker;
    sync_buffer_size = RenderTargetSyncBufferSize;
    dkMemBlockMakerDefaults(&sync_maker, device, AlignUp(sync_buffer_size, DK_MEMBLOCK_ALIGNMENT));
    sync_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    sync_mem_block = dkMemBlockCreate(&sync_maker);
    if (!sync_mem_block) {
        SetError("Failed to create render target sync memory block");
        return false;
    }
    sync_cpu_buffer = dkMemBlockGetCpuAddr(sync_mem_block);
    sync_gpu_addr = dkMemBlockGetGpuAddr(sync_mem_block);
    if (!sync_cpu_buffer || sync_gpu_addr == 0) {
        SetError("Failed to map render target sync memory block");
        return false;
    }

    DkMemBlockMaker cmd_maker;
    dkMemBlockMakerDefaults(&cmd_maker, device,
                            AlignUp(RenderTargetSyncCommandMemorySize, DK_MEMBLOCK_ALIGNMENT));
    cmd_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    sync_command_mem_block = dkMemBlockCreate(&cmd_maker);
    if (!sync_command_mem_block) {
        SetError("Failed to create render target sync command memory");
        return false;
    }
    DkCmdBufMaker cmd_buf_maker;
    dkCmdBufMakerDefaults(&cmd_buf_maker, device);
    sync_command_buffer = dkCmdBufCreate(&cmd_buf_maker);
    if (!sync_command_buffer) {
        SetError("Failed to create render target sync command buffer");
        return false;
    }
    dkCmdBufAddMemory(sync_command_buffer, sync_command_mem_block, 0,
                      RenderTargetSyncCommandMemorySize);
    return true;
}

bool State::WaitForSyncFence() {
    if (!sync_fence_pending) {
        return true;
    }
    FlushQueue();
    RecordFenceWait();
    constexpr s64 FenceWaitTimeoutNs = 1'000'000'000LL;
    const DkResult result = dkFenceWait(&sync_fence, FenceWaitTimeoutNs);
    if (result != DkResult_Success) {
        if (result == DkResult_Timeout) {
            RecordFenceTimeout();
        }
        LOG_WARNING(Render, "Deko3D render target sync fence wait failed result={}",
                    static_cast<int>(result));
        return false;
    }
    sync_fence_pending = false;
    return true;
}

bool State::WaitForSurfaceFence(DisplayTransferSurface& surface) {
    if (!surface.fence_pending) {
        return true;
    }
    FlushQueue();
    RecordFenceWait();
    constexpr s64 FenceWaitTimeoutNs = 1'000'000'000LL;
    const DkResult result = dkFenceWait(&surface.fence, FenceWaitTimeoutNs);
    if (result != DkResult_Success) {
        if (result == DkResult_Timeout) {
            RecordFenceTimeout();
        }
        LOG_WARNING(Render, "Deko3D display surface fence wait failed addr=0x{:08x} result={}",
                    surface.display_address, static_cast<int>(result));
        return false;
    }
    surface.fence_pending = false;
    return true;
}

void State::DestroyDisplayTransferSurface(DisplayTransferSurface& surface) {
    surface.valid = false;
    surface.fence_pending = false;
    if (surface.command_buffer) {
        dkCmdBufDestroy(surface.command_buffer);
        surface.command_buffer = nullptr;
    }
    if (surface.command_mem_block) {
        dkMemBlockDestroy(surface.command_mem_block);
        surface.command_mem_block = nullptr;
    }
    surface.view = {};
    surface.image = {};
    if (surface.mem_block) {
        dkMemBlockDestroy(surface.mem_block);
        surface.mem_block = nullptr;
    }
}

State::DisplayTransferSurface* State::GetOrCreateDisplayTransferSurface(PAddr address, u32 width,
                                                                        u32 height, u32 format,
                                                                        u32 output_bytes_per_pixel) {
    if (address == 0 || width == 0 || height == 0) {
        return nullptr;
    }
    for (auto& surface : display_transfer_surfaces) {
        if (!surface || surface->display_address != address) {
            continue;
        }
        if (surface->width == width && surface->height == height && surface->format == format &&
            surface->output_bytes_per_pixel == output_bytes_per_pixel && surface->mem_block &&
            surface->command_buffer) {
            return surface.get();
        }
        if (!WaitForSurfaceFence(*surface)) {
            return nullptr;
        }
        DestroyDisplayTransferSurface(*surface);
        break;
    }

    auto found = std::find_if(display_transfer_surfaces.begin(), display_transfer_surfaces.end(),
                              [address](const auto& surface) {
                                  return surface && surface->display_address == address;
                              });
    if (found == display_transfer_surfaces.end()) {
        found = display_transfer_surfaces.emplace(display_transfer_surfaces.end(),
                                                  std::make_unique<DisplayTransferSurface>());
    }
    DisplayTransferSurface& surface = **found;
    surface.display_address = address;
    surface.width = width;
    surface.height = height;
    surface.format = format;
    surface.output_bytes_per_pixel = output_bytes_per_pixel;
    surface.completed_generation = 0;
    surface.valid = false;

    const auto mapped_format =
        MapRenderTargetColorFormat(static_cast<Pica::FramebufferRegs::ColorFormat>(format));
    if (!mapped_format) {
        return nullptr;
    }

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    layout_maker.format = *mapped_format;
    layout_maker.dimensions[0] = width;
    layout_maker.dimensions[1] = height;

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layout_maker);
    const u64 image_size = dkImageLayoutGetSize(&layout);
    const u32 image_alignment = dkImageLayoutGetAlignment(&layout);
    const u64 allocation_bytes =
        AlignUp(AlignUp(image_size, image_alignment), DK_MEMBLOCK_ALIGNMENT);
    if (image_size == 0 || image_alignment == 0 ||
        allocation_bytes > std::numeric_limits<u32>::max()) {
        return nullptr;
    }

    DkMemBlockMaker image_maker;
    dkMemBlockMakerDefaults(&image_maker, device, static_cast<u32>(allocation_bytes));
    image_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    surface.mem_block = dkMemBlockCreate(&image_maker);
    if (!surface.mem_block) {
        return nullptr;
    }
    dkImageInitialize(&surface.image, &layout, surface.mem_block, 0);
    dkImageViewDefaults(&surface.view, &surface.image);

    DkMemBlockMaker cmd_maker;
    dkMemBlockMakerDefaults(&cmd_maker, device,
                            AlignUp(DisplayTransferCommandMemorySize, DK_MEMBLOCK_ALIGNMENT));
    cmd_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    surface.command_mem_block = dkMemBlockCreate(&cmd_maker);
    if (!surface.command_mem_block) {
        DestroyDisplayTransferSurface(surface);
        return nullptr;
    }
    DkCmdBufMaker cmd_buf_maker;
    dkCmdBufMakerDefaults(&cmd_buf_maker, device);
    surface.command_buffer = dkCmdBufCreate(&cmd_buf_maker);
    if (!surface.command_buffer) {
        DestroyDisplayTransferSurface(surface);
        return nullptr;
    }
    dkCmdBufAddMemory(surface.command_buffer, surface.command_mem_block, 0,
                      DisplayTransferCommandMemorySize);
    LOG_INFO(Render,
             "Deko3D display surface create: addr=0x{:08x} size={}x{} format={} bpp={} bytes={}",
             address, width, height, format, output_bytes_per_pixel,
             static_cast<unsigned long long>(allocation_bytes));
    return &surface;
}

bool State::DrawPresentImage(const DkImageView& view, u32 source_width, u32 source_height, u32 slot,
                             u32 scratch_index, u32 src_y, u32 src_height, u32 dst_x,
                             u32 dst_y, u32 dst_width, u32 dst_height, const char* label) {
    if (!present_vertex_shader || !present_fragment_shader || !present_cpu_buffer ||
        present_gpu_addr == 0 || slot >= FramebufferCount || src_height == 0 ||
        src_y >= source_height || source_width == 0 || source_height == 0) {
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
    dkImageDescriptorInitialize(&image_descriptor, &view, false, false);
    dkSamplerDescriptorInitialize(&sampler_descriptor, &present_sampler);
    std::memcpy(static_cast<u8*>(present_cpu_buffer) + scratch_base + ImageOffset, &image_descriptor,
                sizeof(image_descriptor));
    std::memcpy(static_cast<u8*>(present_cpu_buffer) + scratch_base + sampler_offset,
                &sampler_descriptor, sizeof(sampler_descriptor));

    const u32 clamped_src_height = std::min(src_height, source_height - src_y);
    const float src_top =
        static_cast<float>(src_y) / static_cast<float>(std::max(source_height, 1u));
    const float src_bottom =
        static_cast<float>(src_y + clamped_src_height) /
        static_cast<float>(std::max(source_height, 1u));
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
    if (now - last_present_shader_log >= std::chrono::seconds(5)) {
        last_present_shader_log = now;
        LOG_INFO(Render,
                 "Present cached {} shader draw src={}x{} y={} h={} dst={}x{} "
                 "uv={:.3f}..{:.3f}",
                 label ? label : "screen", source_width, source_height, src_y,
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
    const u64 allocation_bytes =
        AlignUp(AlignUp(image_size, image_alignment), DK_MEMBLOCK_ALIGNMENT);
    if (image_size == 0 || image_alignment == 0 ||
        allocation_bytes > std::numeric_limits<u32>::max()) {
        return nullptr;
    }

    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device, static_cast<u32>(allocation_bytes));
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    DkMemBlock mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!mem_block) {
        return nullptr;
    }

    auto target = std::make_unique<CachedRenderTarget>();
    target->key = key;
    target->mem_block = mem_block;
    target->allocation_bytes = allocation_bytes;
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
        if (target->key.color_address == address && target->gpu_dirty && !target->cpu_dirty &&
            target->owner == SurfaceOwner::Deko3D) {
            return target.get();
        }
    }
    return nullptr;
}

State::CachedRenderTarget* State::FindRenderTarget(const RenderTargetKey& key) {
    for (auto& target : render_targets) {
        if (target && target->key == key) {
            return target.get();
        }
    }
    return nullptr;
}

State::CachedRenderTarget* State::FindRenderTargetByAddressRange(PAddr address, u32 bytes) {
    if (address == 0 || bytes == 0) {
        return nullptr;
    }

    for (auto& target : render_targets) {
        if (target && target->key.color_address == address &&
            RenderTargetBytes(target->key) == bytes) {
            return target.get();
        }
    }
    return nullptr;
}

State::PresentImage State::GetSelectedPresentImage() const {
    return selected_present_image;
}

State::PresentImage State::GetSelectedBottomPresentImage() const {
    return selected_bottom_present_image;
}

void State::SelectPresentRenderTarget(PAddr address) {
    SelectPresentRenderTargets(address, 0);
}

void State::SelectPresentRenderTargets(PAddr top_address, PAddr bottom_address) {
    selected_present_image = {};
    selected_bottom_present_image = {};

    const auto is_top_shape = [](const CachedRenderTarget* target) {
        return target && target->key.width == 240 && target->key.height >= 400 &&
               target->gpu_dirty && !target->cpu_dirty && target->owner == SurfaceOwner::Deko3D;
    };
    const auto is_bottom_shape = [](const CachedRenderTarget* target) {
        return target && target->key.width == 240 && target->key.height == 320 &&
               target->gpu_dirty && !target->cpu_dirty && target->owner == SurfaceOwner::Deko3D;
    };
    const auto find_surface = [this](PAddr address, bool top_screen) -> PresentImage {
        for (const auto& surface : display_transfer_surfaces) {
            if (surface && surface->display_address == address && surface->valid &&
                surface->completed_generation != 0 && surface->width != 0 &&
                surface->height != 0) {
                const bool valid_shape = top_screen ? (surface->width == 240 &&
                                                       surface->height == 400)
                                                    : (surface->width == 240 &&
                                                       surface->height == 320);
                if (!valid_shape) {
                    continue;
                }
                return PresentImage{
                    .view = &surface->view,
                    .width = surface->width,
                    .height = surface->height,
                    .direct_render_target = false,
                };
            }
        }
        return {};
    };

    selected_present_image = find_surface(top_address, true);
    if (!selected_present_image.IsValid()) {
        const CachedRenderTarget* target = FindGpuDirtyRenderTarget(top_address);
        if (is_top_shape(target)) {
            selected_present_image = PresentImage{
                .view = &target->view,
                .width = target->key.width,
                .height = target->key.height,
                .direct_render_target = true,
            };
        }
    }

    selected_bottom_present_image = bottom_address != 0 ? find_surface(bottom_address, false)
                                                        : PresentImage{};
    if (!selected_bottom_present_image.IsValid() && bottom_address != 0) {
        const CachedRenderTarget* target = FindGpuDirtyRenderTarget(bottom_address);
        if (is_bottom_shape(target)) {
            selected_bottom_present_image = PresentImage{
                .view = &target->view,
                .width = target->key.width,
                .height = target->key.height,
                .direct_render_target = true,
            };
        }
    }

    if (!render_targets.empty() || !display_transfer_surfaces.empty()) {
        static auto s_last_log = std::chrono::steady_clock::time_point{};
        const auto now = std::chrono::steady_clock::now();
        if (now - s_last_log < std::chrono::seconds(5)) {
            return;
        }
        s_last_log = now;
        LOG_INFO(Render,
                 "SelectPresentRenderTarget: top=0x{:08x} top={}x{} direct={} bottom=0x{:08x} "
                 "bottom={}x{} direct={} rt_count={} dt_count={}",
                 top_address, selected_present_image.width, selected_present_image.height,
                 selected_present_image.direct_render_target, bottom_address,
                 selected_bottom_present_image.width, selected_bottom_present_image.height,
                 selected_bottom_present_image.direct_render_target, render_targets.size(),
                 display_transfer_surfaces.size());
    }
}

void State::MarkRenderTargetGpuDirty(CachedRenderTarget& target) {
    target.owner = SurfaceOwner::Deko3D;
    target.gpu_dirty = true;
    target.cpu_dirty = false;
    target.software_locked = false;
    target.deko_generation = ++render_target_generation;
    RecordRenderTargetGpuDirty();
}

bool State::RecordDisplayTransfer(PAddr input_address, PAddr output_address, u32 input_width,
                                  u32 input_height, u32 output_width, u32 output_height,
                                  u32 flags, u32 output_bytes_per_pixel) {
    if (input_address == 0 || output_address == 0) {
        return false;
    }

    const CachedRenderTarget* source = nullptr;
    for (const auto& target : render_targets) {
        if (target->key.color_address == input_address && target->gpu_dirty &&
            !target->cpu_dirty && target->owner == SurfaceOwner::Deko3D) {
            source = target.get();
            break;
        }
    }
    if (!source) {
        for (const auto& target : render_targets) {
            if (!target->gpu_dirty || target->cpu_dirty || target->owner != SurfaceOwner::Deko3D ||
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

    const bool flip_y = (flags & 0x1) != 0;
    const bool crop_input_lines = (flags & (1U << 2)) != 0;
    const u32 scaling = (flags >> 24) & 0x3;
    const bool supported_size =
        input_width == output_width &&
        (input_height == output_height || (crop_input_lines && output_height <= input_height));
    if (!supported_size || scaling != 0) {
        return false;
    }

    DisplayTransferSurface* surface =
        GetOrCreateDisplayTransferSurface(output_address, output_width, output_height,
                                          source->key.format, output_bytes_per_pixel);
    if (!surface || !surface->command_buffer || !WaitForSurfaceFence(*surface)) {
        return false;
    }

    dkCmdBufClear(surface->command_buffer);
    dkCmdBufAddMemory(surface->command_buffer, surface->command_mem_block, 0,
                      DisplayTransferCommandMemorySize);
    DkImageRect src_rect{0, 0, 0, output_width, output_height, 1};
    DkImageRect dst_rect{0, 0, 0, output_width, output_height, 1};
    u32 blit_flags = DkBlitFlag_FilterNearest | DkBlitFlag_ModeBlit;
    if (flip_y) {
        blit_flags |= DkBlitFlag_FlipY;
    }
    dkCmdBufBarrier(surface->command_buffer, DkBarrier_Fragments,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
    dkCmdBufBlitImage(surface->command_buffer, &source->view, &src_rect, &surface->view,
                      &dst_rect, blit_flags, 0);
    dkCmdBufSignalFence(surface->command_buffer, &surface->fence, false);
    const DkCmdList cmd_list = dkCmdBufFinishList(surface->command_buffer);
    if (!cmd_list) {
        surface->valid = false;
        return false;
    }
    dkQueueSubmitCommands(queue, cmd_list);
    RecordPresentQueueSubmit();
    if (QueueHasError("after display transfer snapshot submit")) {
        surface->valid = false;
        return false;
    }
    FlushQueue();
    surface->fence_pending = true;
    surface->valid = true;
    surface->completed_generation = source->deko_generation;

    static auto s_last_transfer_log = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - s_last_transfer_log >= std::chrono::seconds(5)) {
        s_last_transfer_log = now;
    LOG_INFO(Render,
             "Deko3D display transfer snapshot: dst=0x{:08x} src=0x{:08x} rt={}x{} "
             "in={}x{} out={}x{} bpp={} flags=0x{:08x} gen={}",
             output_address, source->key.color_address, source->key.width, source->key.height,
             input_width, input_height, output_width, output_height, output_bytes_per_pixel, flags,
             source->deko_generation);
    }
    return true;
}

void State::InvalidateRenderTargetsOverlapping(PAddr address, u32 bytes, SurfaceOwner owner) {
    if (address == 0 || bytes == 0) {
        return;
    }
    u64 target_invalidation_bytes = bytes;
    for (auto& surface : display_transfer_surfaces) {
        if (surface && surface->display_address == address) {
            target_invalidation_bytes =
                std::min(target_invalidation_bytes, DisplaySurfaceBytes(*surface));
        }
        if (surface && surface->valid &&
            RangesOverlap(address, bytes, surface->display_address, DisplaySurfaceBytes(*surface))) {
            surface->valid = false;
            if (selected_present_image.view == &surface->view) {
                selected_present_image = {};
            }
            if (selected_bottom_present_image.view == &surface->view) {
                selected_bottom_present_image = {};
            }
        }
    }
    for (auto& target : render_targets) {
        const u64 target_bytes = RenderTargetBytes(target->key);
        if (!RangesOverlap(address, target_invalidation_bytes, target->key.color_address,
                           target_bytes)) {
            continue;
        }
        static auto s_last_invalidate_log = std::chrono::steady_clock::time_point{};
        static PAddr s_last_invalidate_address = 0;
        static SurfaceOwner s_last_invalidate_owner = SurfaceOwner::Clean;
        const auto now = std::chrono::steady_clock::now();
        if (s_last_invalidate_address != target->key.color_address ||
            s_last_invalidate_owner != owner ||
            now - s_last_invalidate_log >= std::chrono::seconds(1)) {
            s_last_invalidate_log = now;
            s_last_invalidate_address = target->key.color_address;
            s_last_invalidate_owner = owner;
            LOG_INFO(Render,
                     "Deko3D RT invalidate: owner={} addr=0x{:08x} bytes={} clamped={} "
                     "target=0x{:08x} target_bytes={} state(owner={} gpu={} cpu={} locked={})",
                     SurfaceOwnerName(owner), address, bytes,
                     static_cast<unsigned long long>(target_invalidation_bytes),
                     target->key.color_address, static_cast<unsigned long long>(target_bytes),
                     SurfaceOwnerName(target->owner), target->gpu_dirty, target->cpu_dirty,
                     target->software_locked);
        }
        // Ownership is exclusive: GPU-owned images are valid for Deko3D draws, while CPU/software
        // owned targets must be uploaded before hardware rendering can safely resume.
        target->owner = owner;
        target->gpu_dirty = false;
        target->cpu_dirty = true;
        target->software_locked = owner == SurfaceOwner::SoftwareRasterizer;
        target->guest_memory_generation = ++render_target_generation;
        RecordRenderTargetCpuDirty();
        if (selected_present_image.view == &target->view) {
            selected_present_image = {};
        }
        if (selected_bottom_present_image.view == &target->view) {
            selected_bottom_present_image = {};
        }
    }
}

void State::MarkRenderTargetSoftwareDirty(PAddr address, u32 bytes) {
    InvalidateRenderTargetsOverlapping(address, bytes, SurfaceOwner::SoftwareRasterizer);
}

void State::MarkRenderTargetDisplayTransferWrite(PAddr address, u32 bytes) {
    InvalidateRenderTargetsOverlapping(address, bytes, SurfaceOwner::DisplayTransfer);
}

bool State::PrepareRenderTargetForSoftware(CachedRenderTarget& target,
                                           Memory::MemorySystem& memory) {
    if (!target.gpu_dirty || target.cpu_dirty || target.owner != SurfaceOwner::Deko3D) {
        target.software_locked = true;
        return true;
    }
    if (!sync_command_buffer || !sync_cpu_buffer || sync_gpu_addr == 0 || !WaitForSyncFence()) {
        return false;
    }

    const u64 bytes = RenderTargetBytes(target.key);
    if (bytes == 0 || bytes > sync_buffer_size || bytes > std::numeric_limits<u32>::max()) {
        LOG_WARNING(Render, "Deko3D readback skipped: target too large addr=0x{:08x} bytes={}",
                    target.key.color_address, static_cast<unsigned long long>(bytes));
        return false;
    }
    u8* guest = memory.GetPhysicalPointer(target.key.color_address);
    if (!guest) {
        LOG_WARNING(Render, "Deko3D readback skipped: guest target unmapped addr=0x{:08x}",
                    target.key.color_address);
        return false;
    }

    dkCmdBufClear(sync_command_buffer);
    dkCmdBufAddMemory(sync_command_buffer, sync_command_mem_block, 0,
                      RenderTargetSyncCommandMemorySize);
    DkImageRect src_rect{0, 0, 0, target.key.width, target.key.height, 1};
    DkCopyBuf dst_buf{sync_gpu_addr, 0, 0};
    dkCmdBufBarrier(sync_command_buffer, DkBarrier_Fragments, DkInvalidateFlags_Image);
    dkCmdBufCopyImageToBuffer(sync_command_buffer, &target.view, &src_rect, &dst_buf, 0);
    dkCmdBufSignalFence(sync_command_buffer, &sync_fence, false);
    const DkCmdList cmd_list = dkCmdBufFinishList(sync_command_buffer);
    if (!cmd_list) {
        return false;
    }
    dkQueueSubmitCommands(queue, cmd_list);
    RecordPresentQueueSubmit();
    FlushQueue();
    sync_fence_pending = true;
    if (QueueHasError("after render target readback submit") || !WaitForSyncFence()) {
        return false;
    }

    const auto format = static_cast<Pica::FramebufferRegs::ColorFormat>(target.key.format);
    const u32 bpp = Pica::FramebufferRegs::BytesPerColorPixel(format);
    const auto* linear = static_cast<const u8*>(sync_cpu_buffer);
    for (u32 y = 0; y < target.key.height; ++y) {
        const u32 guest_y = target.key.height - 1 - y;
        for (u32 x = 0; x < target.key.width; ++x) {
            const u8* src = linear + (static_cast<std::size_t>(y) * target.key.width + x) * bpp;
            u8* dst = guest + GuestOffset(x, guest_y, target.key.width, bpp);
            EncodeGuestColor(DecodeDekoColor(src, format), dst, format);
        }
    }

    target.owner = SurfaceOwner::SoftwareRasterizer;
    target.gpu_dirty = false;
    target.cpu_dirty = true;
    target.software_locked = true;
    target.guest_memory_generation = ++render_target_generation;
    RecordRenderTargetCpuDirty();
    LOG_INFO(Render, "Deko3D RT readback for software addr=0x{:08x} size={}x{}",
             target.key.color_address, target.key.width, target.key.height);
    return true;
}

bool State::PrepareRenderTargetForHardware(CachedRenderTarget& target,
                                           Memory::MemorySystem& memory) {
    if (!target.cpu_dirty) {
        return true;
    }
    if (target.software_locked) {
        return false;
    }
    if (!sync_command_buffer || !sync_cpu_buffer || sync_gpu_addr == 0 || !WaitForSyncFence()) {
        return false;
    }

    const u64 bytes = RenderTargetBytes(target.key);
    if (bytes == 0 || bytes > sync_buffer_size || bytes > std::numeric_limits<u32>::max()) {
        LOG_WARNING(Render, "Deko3D upload skipped: target too large addr=0x{:08x} bytes={}",
                    target.key.color_address, static_cast<unsigned long long>(bytes));
        return false;
    }
    const u8* guest = memory.GetPhysicalPointer(target.key.color_address);
    if (!guest) {
        LOG_WARNING(Render, "Deko3D upload skipped: guest target unmapped addr=0x{:08x}",
                    target.key.color_address);
        return false;
    }

    const auto format = static_cast<Pica::FramebufferRegs::ColorFormat>(target.key.format);
    const u32 bpp = Pica::FramebufferRegs::BytesPerColorPixel(format);
    auto* linear = static_cast<u8*>(sync_cpu_buffer);
    for (u32 y = 0; y < target.key.height; ++y) {
        const u32 guest_y = target.key.height - 1 - y;
        for (u32 x = 0; x < target.key.width; ++x) {
            const u8* src = guest + GuestOffset(x, guest_y, target.key.width, bpp);
            u8* dst = linear + (static_cast<std::size_t>(y) * target.key.width + x) * bpp;
            EncodeDekoColor(DecodeGuestColor(src, format), dst, format);
        }
    }

    dkCmdBufClear(sync_command_buffer);
    dkCmdBufAddMemory(sync_command_buffer, sync_command_mem_block, 0,
                      RenderTargetSyncCommandMemorySize);
    DkCopyBuf src_buf{sync_gpu_addr, 0, 0};
    DkImageRect dst_rect{0, 0, 0, target.key.width, target.key.height, 1};
    dkCmdBufCopyBufferToImage(sync_command_buffer, &src_buf, &target.view, &dst_rect, 0);
    dkCmdBufSignalFence(sync_command_buffer, &sync_fence, false);
    const DkCmdList cmd_list = dkCmdBufFinishList(sync_command_buffer);
    if (!cmd_list) {
        return false;
    }
    dkQueueSubmitCommands(queue, cmd_list);
    RecordPresentQueueSubmit();
    if (QueueHasError("after render target upload submit")) {
        return false;
    }
    FlushQueue();
    sync_fence_pending = true;

    target.owner = SurfaceOwner::Deko3D;
    target.gpu_dirty = true;
    target.cpu_dirty = false;
    target.needs_clear = false;
    target.deko_generation = ++render_target_generation;
    RecordRenderTargetGpuDirty();
    LOG_INFO(Render, "Deko3D RT upload for hardware addr=0x{:08x} size={}x{}",
             target.key.color_address, target.key.width, target.key.height);
    return true;
}

void State::UnlockSoftwareRenderTarget(PAddr address) {
    for (auto& target : render_targets) {
        if (!target || !target->software_locked) {
            continue;
        }
        if (target->key.color_address == address ||
            RangesOverlap(address, 1, target->key.color_address, RenderTargetBytes(target->key))) {
            target->software_locked = false;
        }
    }
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
    const PresentImage cached_present = selected_present_image;
    const PresentImage cached_bottom_present = selected_bottom_present_image;
    if (!cached_present.IsValid()) {
        std::memcpy(upload_ptr, top_src, top_bytes);
    }
    if (!cached_bottom_present.IsValid()) {
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
    if (cached_present.IsValid()) {
        const u32 top_src_y = 0;
        const u32 top_src_height =
            cached_present.direct_render_target ? std::min(400u, cached_present.height - top_src_y)
                                                : cached_present.height;
        if (!DrawPresentImage(*cached_present.view, cached_present.width, cached_present.height,
                              static_cast<u32>(slot), 0, top_src_y, top_src_height, top_x, top_y,
                              top_width, top_height, "top")) {
            DkImageRect rt_src = {0, top_src_y, 0, cached_present.width, top_src_height, 1};
            dkCmdBufBarrier(cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
            dkCmdBufBlitImage(cmdbuf, cached_present.view, &rt_src,
                              &framebuffer_views[slot], &top_copy_dst,
                              DkBlitFlag_FilterNearest | DkBlitFlag_ModeBlit, 0);
            LOG_INFO(Render, "Present cached top shader unavailable; blit fallback src={}x{} "
                             "dst={}x{}",
                     cached_present.width, top_src_height, top_copy_dst.width,
                     top_copy_dst.height);
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
    if (cached_bottom_present.IsValid() && cached_bottom_present.width == 240 &&
        cached_bottom_present.height == 320) {
        if (DrawPresentImage(*cached_bottom_present.view, cached_bottom_present.width,
                             cached_bottom_present.height, static_cast<u32>(slot), 1, 0,
                             cached_bottom_present.height, bottom_x, bottom_y, bottom_width,
                             bottom_height, "bottom")) {
            bottom_from_rt = true;
        } else {
            DkImageRect bottom_rt_src = {0, 0, 0, cached_bottom_present.width,
                                         std::min(cached_bottom_present.height, 320u), 1};
            dkCmdBufBarrier(cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
            dkCmdBufBlitImage(cmdbuf, cached_bottom_present.view, &bottom_rt_src,
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
