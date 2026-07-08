// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_state.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>
#include <new>

#include "common/logging/log.h"
#include "common/switch_trace.h"
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
u64 AlignUp(u64 value, u64 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

DkImageFormat MapColorFormat(u32 format) {
    using Color = Pica::FramebufferRegs::ColorFormat;
    switch (static_cast<Color>(format)) {
    case Color::RGBA8:
        return DkImageFormat_RGBA8_Unorm;
    case Color::RGB8:
        // Maxwell has no renderable packed RGB8 target. Use RGBX8 while preserving guest range size.
        return DkImageFormat_RGBX8_Unorm;
    case Color::RGB5A1:
        return DkImageFormat_RGB5A1_Unorm;
    case Color::RGB565:
        return DkImageFormat_RGB565_Unorm;
    case Color::RGBA4:
        return DkImageFormat_RGBA4_Unorm;
    }
    return DkImageFormat_None;
}

u64 GuestRenderTargetBytes(const State::RenderTargetKey& key) {
    using Color = Pica::FramebufferRegs::ColorFormat;
    const u32 bpp = Pica::FramebufferRegs::BytesPerColorPixel(static_cast<Color>(key.format));
    return static_cast<u64>(key.width) * key.height * bpp;
}

bool RangesOverlap(PAddr lhs, u64 lhs_size, PAddr rhs, u64 rhs_size) {
    if (lhs_size == 0 || rhs_size == 0) {
        return false;
    }
    return static_cast<u64>(lhs) < static_cast<u64>(rhs) + rhs_size &&
           static_cast<u64>(rhs) < static_cast<u64>(lhs) + lhs_size;
}

void Deko3DDebugCallback(void* user_data, const char* context, DkResult result,
                         const char* message) {
    LOG_ERROR(Render, "Deko3D validation: user_data={} context={} result={} message={}", user_data,
              context ? context : "", static_cast<int>(result), message ? message : "");
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
    if (initialized) {
        return true;
    }
    if (!CreateDevice() || !CreateCommandBuffer() || !CreateQueue() || !CreateFramebuffers() ||
        !RecordStaticCommands() || !CreateScreenTextures()) {
        Shutdown();
        return false;
    }

    constexpr u32 TopBytes = 400 * 240 * 4;
    constexpr u32 BottomBytes = 320 * 240 * 4;
    screen_data_buffer_size = TopBytes + BottomBytes;
    screen_data_buffer = new (std::nothrow) u8[screen_data_buffer_size]{};
    if (!screen_data_buffer) {
        SetError("Failed to allocate screen staging data");
        Shutdown();
        return false;
    }

    upload_buffer_size = screen_data_buffer_size;
    DkMemBlockMaker upload_maker;
    dkMemBlockMakerDefaults(&upload_maker, device,
                            static_cast<u32>(AlignUp(upload_buffer_size, DK_MEMBLOCK_ALIGNMENT)));
    upload_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    upload_mem_block = dkMemBlockCreate(&upload_maker);
    if (!upload_mem_block) {
        SetError("Failed to create presentation upload memory");
        Shutdown();
        return false;
    }
    upload_cpu_buffer = dkMemBlockGetCpuAddr(upload_mem_block);
    upload_gpu_addr = dkMemBlockGetGpuAddr(upload_mem_block);
    if (!upload_cpu_buffer || upload_gpu_addr == DK_GPU_ADDR_INVALID) {
        SetError("Failed to map presentation upload memory");
        Shutdown();
        return false;
    }

    initialized = true;
    LOG_INFO(Render,
             "Deko3D native state initialized: output={}x{} buffers={} queue=graphics+compute+zcull",
             FramebufferWidth, FramebufferHeight, FramebufferCount);
    return true;
#endif
}

void State::Shutdown() {
#ifdef __SWITCH__
    if (queue) {
        dkQueueWaitIdle(queue);
    }
    selected_present_render_target = nullptr;
    present_fence_pending = false;
    present_fence = {};
    for (auto& target : render_targets) {
        if (target.mem_block) {
            dkMemBlockDestroy(target.mem_block);
            target.mem_block = nullptr;
        }
    }
    render_targets.clear();
    render_target_generation = 0;

    if (swapchain) {
        dkSwapchainDestroy(swapchain);
        swapchain = nullptr;
    }
    framebuffer_views = {};
    framebuffers = {};
    swapchain_images.fill(nullptr);
    if (framebuffer_mem_block) {
        dkMemBlockDestroy(framebuffer_mem_block);
        framebuffer_mem_block = nullptr;
    }
    framebuffer_cpu_buffer = nullptr;
    framebuffer_image_stride = 0;

    if (screen_tex_mem_block) {
        dkMemBlockDestroy(screen_tex_mem_block);
        screen_tex_mem_block = nullptr;
    }
    delete top_screen_view;
    delete bottom_screen_view;
    delete top_screen_image;
    delete bottom_screen_image;
    top_screen_view = nullptr;
    bottom_screen_view = nullptr;
    top_screen_image = nullptr;
    bottom_screen_image = nullptr;

    if (upload_mem_block) {
        dkMemBlockDestroy(upload_mem_block);
        upload_mem_block = nullptr;
    }
    upload_cpu_buffer = nullptr;
    upload_gpu_addr = 0;
    upload_buffer_size = 0;

    clear_cmd = 0;
    bind_framebuffer_cmds.fill(0);
    if (cmdbuf) {
        dkCmdBufDestroy(cmdbuf);
        cmdbuf = nullptr;
    }
    if (cmdbuf_mem_block) {
        dkMemBlockDestroy(cmdbuf_mem_block);
        cmdbuf_mem_block = nullptr;
    }
    if (queue) {
        dkQueueDestroy(queue);
        queue = nullptr;
    }
    if (device) {
        dkDeviceDestroy(device);
        device = nullptr;
    }
#endif
    delete[] static_cast<u8*>(screen_data_buffer);
    screen_data_buffer = nullptr;
    screen_data_buffer_size = 0;
    top_screen_gpu_dirty = false;
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
    (void)red;
    (void)green;
    (void)blue;
    (void)alpha;
    return false;
#else
    if (!initialized || !queue || !swapchain) {
        return false;
    }
    const int slot = dkQueueAcquireImage(queue, swapchain);
    if (slot < 0 || slot >= static_cast<int>(FramebufferCount)) {
        return false;
    }
    if (present_fence_pending && dkFenceWait(&present_fence, 1'000'000'000LL) != DkResult_Success) {
        return false;
    }
    present_fence_pending = false;
    dkCmdBufClear(cmdbuf);
    dkCmdBufBindRenderTarget(cmdbuf, &framebuffer_views[slot], nullptr);
    dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, red, green, blue, alpha);
    const DkCmdList list = dkCmdBufFinishList(cmdbuf);
    if (!list) {
        return false;
    }
    dkQueueSubmitCommands(queue, list);
    dkQueueSignalFence(queue, &present_fence, true);
    dkQueueFlush(queue);
    present_fence_pending = true;
    dkQueuePresentImage(queue, swapchain, slot);
    return !QueueHasError("after clear present");
#endif
}

#ifdef __SWITCH__
bool State::CreateDevice() {
    DkDeviceMaker maker;
    dkDeviceMakerDefaults(&maker);
#ifdef AZAHAR_DEKO3D_VALIDATION
    maker.userData = this;
    maker.cbDebug = Deko3DDebugCallback;
#endif
    device = dkDeviceCreate(&maker);
    if (!device) {
        SetError("dkDeviceCreate failed");
        return false;
    }
    return true;
}

bool State::CreateCommandBuffer() {
    DkMemBlockMaker memory_maker;
    dkMemBlockMakerDefaults(&memory_maker, device, CommandMemorySize);
    memory_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    cmdbuf_mem_block = dkMemBlockCreate(&memory_maker);
    if (!cmdbuf_mem_block) {
        SetError("Deko3D presentation command memory allocation failed");
        return false;
    }
    DkCmdBufMaker command_maker;
    dkCmdBufMakerDefaults(&command_maker, device);
    cmdbuf = dkCmdBufCreate(&command_maker);
    if (!cmdbuf) {
        SetError("Deko3D presentation command buffer creation failed");
        return false;
    }
    dkCmdBufAddMemory(cmdbuf, cmdbuf_mem_block, 0, CommandMemorySize);
    return true;
}

bool State::CreateQueue() {
    DkQueueMaker maker;
    dkQueueMakerDefaults(&maker, device);
    maker.flags = DkQueueFlags_Graphics | DkQueueFlags_Compute | DkQueueFlags_MediumPrio |
                  DkQueueFlags_EnableZcull;
    queue = dkQueueCreate(&maker);
    if (!queue) {
        SetError("dkQueueCreate failed");
        return false;
    }
    return true;
}

bool State::CreateFramebuffers() {
    NWindow* const window = nwindowGetDefault();
    if (!window || !nwindowIsValid(window)) {
        SetError("Default Switch window is unavailable");
        return false;
    }

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_UsagePresent |
                         DkImageFlags_Usage2DEngine;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
    layout_maker.dimensions[0] = FramebufferWidth;
    layout_maker.dimensions[1] = FramebufferHeight;
    DkImageLayout layout{};
    dkImageLayoutInitialize(&layout, &layout_maker);
    const u64 image_size = dkImageLayoutGetSize(&layout);
    const u32 alignment = dkImageLayoutGetAlignment(&layout);
    const u64 stride = AlignUp(image_size, alignment);
    const u64 total_size = stride * FramebufferCount;
    if (image_size == 0 || alignment == 0 || total_size > std::numeric_limits<u32>::max()) {
        SetError("Invalid Deko3D swapchain image layout");
        return false;
    }

    DkMemBlockMaker memory_maker;
    dkMemBlockMakerDefaults(&memory_maker, device, static_cast<u32>(total_size));
    memory_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    framebuffer_mem_block = dkMemBlockCreate(&memory_maker);
    if (!framebuffer_mem_block) {
        SetError("Deko3D swapchain memory allocation failed");
        return false;
    }
    for (u32 index = 0; index < FramebufferCount; ++index) {
        dkImageInitialize(&framebuffers[index], &layout, framebuffer_mem_block,
                          static_cast<u32>(stride * index));
        dkImageViewDefaults(&framebuffer_views[index], &framebuffers[index]);
        swapchain_images[index] = &framebuffers[index];
    }
    DkSwapchainMaker swapchain_maker;
    dkSwapchainMakerDefaults(&swapchain_maker, device, window, swapchain_images.data(),
                             FramebufferCount);
    swapchain = dkSwapchainCreate(&swapchain_maker);
    if (!swapchain) {
        SetError("Deko3D swapchain creation failed");
        return false;
    }
    dkSwapchainSetSwapInterval(swapchain, 1);
    return true;
}

bool State::RecordStaticCommands() {
    bind_framebuffer_cmds.fill(0);
    clear_cmd = 0;
    return true;
}

bool State::CreateScreenTextures() {
    constexpr u32 TopWidth = 400;
    constexpr u32 TopHeight = 240;
    constexpr u32 BottomWidth = 320;
    constexpr u32 BottomHeight = 240;

    DkImageLayoutMaker maker;
    dkImageLayoutMakerDefaults(&maker, device);
    maker.type = DkImageType_2D;
    maker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine;
    maker.format = DkImageFormat_RGBA8_Unorm;
    maker.dimensions[0] = TopWidth;
    maker.dimensions[1] = TopHeight;
    DkImageLayout top_layout{};
    dkImageLayoutInitialize(&top_layout, &maker);
    const u64 top_size = AlignUp(dkImageLayoutGetSize(&top_layout),
                                 dkImageLayoutGetAlignment(&top_layout));

    maker.dimensions[0] = BottomWidth;
    maker.dimensions[1] = BottomHeight;
    DkImageLayout bottom_layout{};
    dkImageLayoutInitialize(&bottom_layout, &maker);
    const u64 bottom_size = AlignUp(dkImageLayoutGetSize(&bottom_layout),
                                    dkImageLayoutGetAlignment(&bottom_layout));
    if (top_size + bottom_size > std::numeric_limits<u32>::max()) {
        return false;
    }
    DkMemBlockMaker memory_maker;
    dkMemBlockMakerDefaults(&memory_maker, device, static_cast<u32>(top_size + bottom_size));
    memory_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    screen_tex_mem_block = dkMemBlockCreate(&memory_maker);
    if (!screen_tex_mem_block) {
        return false;
    }
    top_screen_image = new (std::nothrow) DkImage{};
    bottom_screen_image = new (std::nothrow) DkImage{};
    top_screen_view = new (std::nothrow) DkImageView{};
    bottom_screen_view = new (std::nothrow) DkImageView{};
    if (!top_screen_image || !bottom_screen_image || !top_screen_view || !bottom_screen_view) {
        return false;
    }
    dkImageInitialize(top_screen_image, &top_layout, screen_tex_mem_block, 0);
    dkImageInitialize(bottom_screen_image, &bottom_layout, screen_tex_mem_block,
                      static_cast<u32>(top_size));
    dkImageViewDefaults(top_screen_view, top_screen_image);
    dkImageViewDefaults(bottom_screen_view, bottom_screen_image);
    return true;
}

bool State::QueueHasError(const char* context) {
    if (!queue || !dkQueueIsInErrorState(queue)) {
        return false;
    }
    RecordQueueError();
    RecordPresentQueueError();
    LOG_ERROR(Render, "Deko3D queue error {}", context ? context : "");
    return true;
}

void State::FlushQueue() {
    if (queue) {
        dkQueueFlush(queue);
        RecordQueueFlush();
        RecordPresentQueueFlush();
    }
}

State::CachedRenderTarget* State::GetOrCreateRenderTarget(const RenderTargetKey& key) {
    if (!initialized || !device || key.color_address == 0 || key.width == 0 || key.height == 0) {
        return nullptr;
    }
    for (auto& target : render_targets) {
        if (target.key == key) {
            RecordRenderTargetCacheHit();
            return &target;
        }
    }
    const DkImageFormat host_format = MapColorFormat(key.format);
    if (host_format == DkImageFormat_None) {
        return nullptr;
    }
    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_UsageRender | DkImageFlags_Usage2DEngine |
                         DkImageFlags_UsageLoadStore;
    layout_maker.format = host_format;
    layout_maker.dimensions[0] = key.width;
    layout_maker.dimensions[1] = key.height;
    DkImageLayout layout{};
    dkImageLayoutInitialize(&layout, &layout_maker);
    const u64 image_size = dkImageLayoutGetSize(&layout);
    const u32 alignment = dkImageLayoutGetAlignment(&layout);
    if (image_size == 0 || image_size > std::numeric_limits<u32>::max()) {
        return nullptr;
    }
    DkMemBlockMaker memory_maker;
    dkMemBlockMakerDefaults(&memory_maker, device,
                            static_cast<u32>(AlignUp(image_size, alignment)));
    memory_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    DkMemBlock memory = dkMemBlockCreate(&memory_maker);
    if (!memory) {
        return nullptr;
    }
    CachedRenderTarget target{};
    target.key = key;
    target.mem_block = memory;
    target.allocation_bytes = AlignUp(image_size, alignment);
    dkImageInitialize(&target.image, &layout, target.mem_block, 0);
    dkImageViewDefaults(&target.view, &target.image);
    render_targets.push_back(target);
    RecordRenderTargetCacheMiss();
    RecordRenderTargetCacheCreation(target.allocation_bytes);
    LOG_INFO(Render, "Deko3D native target create: addr=0x{:08x} size={}x{} guest_format={} host_format={} bytes={}",
             key.color_address, key.width, key.height, key.format, static_cast<u32>(host_format),
             static_cast<unsigned long long>(target.allocation_bytes));
    return &render_targets.back();
}

const State::CachedRenderTarget* State::FindGpuDirtyRenderTarget(PAddr address) const {
    for (const auto& target : render_targets) {
        if (target.key.color_address == address && target.gpu_dirty) {
            return &target;
        }
    }
    return nullptr;
}

const State::CachedRenderTarget* State::GetSelectedPresentRenderTarget() const {
    return selected_present_render_target;
}

void State::SelectPresentRenderTarget(PAddr address) {
    selected_present_render_target = FindGpuDirtyRenderTarget(address);
}

void State::MarkRenderTargetGpuDirty(CachedRenderTarget& target) {
    target.owner = SurfaceOwner::Deko3D;
    target.gpu_dirty = true;
    target.cpu_dirty = false;
    target.deko_generation = ++render_target_generation;
    top_screen_gpu_dirty = true;
    RecordRenderTargetGpuDirty();
}

void State::InvalidateRenderTargetsOverlapping(PAddr address, u32 bytes, SurfaceOwner owner) {
    if (address == 0 || bytes == 0) {
        return;
    }
    for (auto& target : render_targets) {
        if (!RangesOverlap(address, bytes, target.key.color_address,
                           GuestRenderTargetBytes(target.key))) {
            continue;
        }
        target.owner = owner;
        target.gpu_dirty = false;
        target.cpu_dirty = true;
        target.guest_memory_generation = ++render_target_generation;
        RecordRenderTargetCpuDirty();
        if (selected_present_render_target == &target) {
            selected_present_render_target = nullptr;
        }
    }
}

void State::MarkRenderTargetSoftwareDirty(PAddr address, u32 bytes) {
    InvalidateRenderTargetsOverlapping(address, bytes, SurfaceOwner::SoftwareRasterizer);
}

void State::MarkRenderTargetDisplayTransferWrite(PAddr address, u32 bytes) {
    InvalidateRenderTargetsOverlapping(address, bytes, SurfaceOwner::DisplayTransfer);
}

void State::UploadScreenTextures() {}

bool State::PresentScreenTexturesFrame() {
    if (!initialized || !queue || !swapchain || !screen_data_buffer || !upload_cpu_buffer) {
        return false;
    }
    if (present_fence_pending) {
        const DkResult poll = dkFenceWait(&present_fence, 0);
        if (poll == DkResult_Timeout &&
            dkFenceWait(&present_fence, 1'000'000'000LL) != DkResult_Success) {
            RecordPresentFenceTimeout();
            return false;
        }
        present_fence_pending = false;
    }

    const int slot = dkQueueAcquireImage(queue, swapchain);
    if (slot < 0 || slot >= static_cast<int>(FramebufferCount)) {
        return false;
    }
    constexpr u32 TopWidth = 400;
    constexpr u32 TopHeight = 240;
    constexpr u32 BottomWidth = 320;
    constexpr u32 BottomHeight = 240;
    constexpr u32 TopBytes = TopWidth * TopHeight * 4;
    constexpr u32 BottomBytes = BottomWidth * BottomHeight * 4;
    std::memcpy(upload_cpu_buffer, screen_data_buffer, TopBytes + BottomBytes);

    dkCmdBufClear(cmdbuf);
    dkCmdBufBindRenderTarget(cmdbuf, &framebuffer_views[slot], nullptr);
    dkCmdBufClearColorFloat(cmdbuf, 0, DkColorMask_RGBA, 0.0f, 0.0f, 0.0f, 1.0f);

    const u32 top_x = (FramebufferWidth - TopWidth) / 2;
    const u32 bottom_x = (FramebufferWidth - BottomWidth) / 2;
    DkImageRect top_destination{top_x, 40, 0, TopWidth, TopHeight, 1};
    if (selected_present_render_target && selected_present_render_target->gpu_dirty) {
        const auto& target = *selected_present_render_target;
        DkImageRect source{0, 0, 0, target.key.width, target.key.height, 1};
        u32 flags = DkBlitFlag_ModeBlit | DkBlitFlag_FilterLinear;
        if (target.key.width == TopHeight && target.key.height >= TopWidth) {
            // 3DS render targets are commonly portrait-oriented in memory.
            flags |= DkBlitFlag_FlipY;
        }
        dkCmdBufBarrier(cmdbuf, DkBarrier_Fragments, DkInvalidateFlags_Image);
        dkCmdBufBlitImage(cmdbuf, &target.view, &source, &framebuffer_views[slot], &top_destination,
                          flags, 0);
    } else {
        const DkCopyBuf top_source{upload_gpu_addr, TopWidth, TopHeight};
        dkCmdBufCopyBufferToImage(cmdbuf, &top_source, &framebuffer_views[slot], &top_destination, 0);
    }

    const DkCopyBuf bottom_source{upload_gpu_addr + TopBytes, BottomWidth, BottomHeight};
    const DkImageRect bottom_destination{bottom_x, 320, 0, BottomWidth, BottomHeight, 1};
    dkCmdBufCopyBufferToImage(cmdbuf, &bottom_source, &framebuffer_views[slot],
                              &bottom_destination, 0);
    dkCmdBufSignalFence(cmdbuf, &present_fence, true);
    const DkCmdList list = dkCmdBufFinishList(cmdbuf);
    if (!list) {
        return false;
    }
    dkQueueSubmitCommands(queue, list);
    RecordPresentQueueSubmit();
    FlushQueue();
    present_fence_pending = true;
    dkQueuePresentImage(queue, swapchain, slot);
    return !QueueHasError("after native presentation");
}
#endif

} // namespace VideoCore::Deko3D
