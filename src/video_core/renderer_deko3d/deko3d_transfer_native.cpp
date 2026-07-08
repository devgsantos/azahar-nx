// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_rasterizer.h"

#include <algorithm>
#include <cstring>
#include <limits>

#include "common/alignment.h"
#include "common/color.h"
#include "common/logging/log.h"
#include "core/memory.h"
#include "video_core/utils.h"

namespace VideoCore::Deko3D {
namespace {

#ifdef __SWITCH__
u32 AlignUp(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

Common::Vec4<u8> DecodePixel(Pica::PixelFormat format, const u8* source) {
    switch (format) {
    case Pica::PixelFormat::RGBA8:
        return Common::Color::DecodeRGBA8(source);
    case Pica::PixelFormat::RGB8:
        return Common::Color::DecodeRGB8(source);
    case Pica::PixelFormat::RGB565:
        return Common::Color::DecodeRGB565(source);
    case Pica::PixelFormat::RGB5A1:
        return Common::Color::DecodeRGB5A1(source);
    case Pica::PixelFormat::RGBA4:
        return Common::Color::DecodeRGBA4(source);
    }
    return {0, 0, 0, 0};
}
#endif

} // namespace

#ifdef __SWITCH__
u32 Rasterizer::CanonicalColorFormat(Pica::PixelFormat format) {
    using FramebufferColor = Pica::FramebufferRegs::ColorFormat;
    switch (format) {
    case Pica::PixelFormat::RGBA8:
        return static_cast<u32>(FramebufferColor::RGBA8);
    case Pica::PixelFormat::RGB8:
        return static_cast<u32>(FramebufferColor::RGB8);
    case Pica::PixelFormat::RGB565:
        return static_cast<u32>(FramebufferColor::RGB565);
    case Pica::PixelFormat::RGB5A1:
        return static_cast<u32>(FramebufferColor::RGB5A1);
    case Pica::PixelFormat::RGBA4:
        return static_cast<u32>(FramebufferColor::RGBA4);
    }
    return static_cast<u32>(FramebufferColor::RGBA8);
}

Rasterizer::TransferSource* Rasterizer::GetOrCreateTransferSource(
    const Pica::DisplayTransferConfig& config) {
    TransferSourceKey key{
        .address = config.GetPhysicalInputAddress(),
        .width = config.input_width.Value(),
        .height = config.input_height.Value(),
        .format = config.input_format.Value(),
        .linear = config.input_linear.Value() != 0,
    };
    if (key.address == 0 || key.width == 0 || key.height == 0) {
        return nullptr;
    }
    for (auto& source : transfer_sources) {
        if (source->key == key) {
            return source.get();
        }
    }

    auto source = std::make_unique<TransferSource>();
    source->key = key;
    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_Usage2DEngine;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
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
                            AlignUp(static_cast<u32>(image_size), alignment));
    memory_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    source->mem = dkMemBlockCreate(&memory_maker);
    if (!source->mem) {
        return nullptr;
    }
    dkImageInitialize(&source->image, &layout, source->mem, 0);
    dkImageViewDefaults(&source->view, &source->image);
    TransferSource* const result = source.get();
    transfer_sources.emplace_back(std::move(source));
    return result;
}

Rasterizer::TransferUploadContext* Rasterizer::AcquireTransferUploadContext(u32 required_bytes) {
    TransferUploadContext& context = transfer_upload_contexts[current_transfer_upload_context];
    current_transfer_upload_context =
        (current_transfer_upload_context + 1) % TransferUploadContextCount;
    if (context.fence_pending) {
        FlushQueueIfNeeded(true);
        if (dkFenceWait(&context.fence, 500'000'000LL) != DkResult_Success) {
            RecordRasterFenceTimeout();
            return nullptr;
        }
        context.fence_pending = false;
    }
    if (context.capacity >= required_bytes && context.mem && context.cpu &&
        context.gpu != DK_GPU_ADDR_INVALID) {
        return &context;
    }
    if (context.mem) {
        dkMemBlockDestroy(context.mem);
        context = {};
        context.gpu = DK_GPU_ADDR_INVALID;
    }
    const u32 allocation = AlignUp(required_bytes, DK_MEMBLOCK_ALIGNMENT);
    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device, allocation);
    maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    context.mem = dkMemBlockCreate(&maker);
    if (!context.mem) {
        return nullptr;
    }
    context.cpu = dkMemBlockGetCpuAddr(context.mem);
    context.gpu = dkMemBlockGetGpuAddr(context.mem);
    context.capacity = allocation;
    if (!context.cpu || context.gpu == DK_GPU_ADDR_INVALID) {
        dkMemBlockDestroy(context.mem);
        context = {};
        context.gpu = DK_GPU_ADDR_INVALID;
        return nullptr;
    }
    return &context;
}

bool Rasterizer::DecodeTransferSource(const Pica::DisplayTransferConfig& config, void* destination,
                                      u32 destination_size) const {
    const u32 width = config.input_width.Value();
    const u32 height = config.input_height.Value();
    const u64 required = static_cast<u64>(width) * height * 4;
    if (!destination || required == 0 || required > destination_size) {
        return false;
    }
    const u8* const source = memory.GetPhysicalPointer(config.GetPhysicalInputAddress());
    if (!source) {
        return false;
    }
    const auto format = config.input_format.Value();
    const u32 bytes_per_pixel = Pica::BytesPerPixel(format);
    u8* const output = static_cast<u8*>(destination);
    for (u32 y = 0; y < height; ++y) {
        for (u32 x = 0; x < width; ++x) {
            u32 source_offset = 0;
            if (config.input_linear) {
                source_offset = (x + y * width) * bytes_per_pixel;
            } else {
                const u32 coarse_y = y & ~7U;
                source_offset = VideoCore::GetMortonOffset(x, y, bytes_per_pixel) +
                                coarse_y * width * bytes_per_pixel;
            }
            const auto color = DecodePixel(format, source + source_offset);
            u8* const pixel = output + (static_cast<std::size_t>(y) * width + x) * 4;
            pixel[0] = color.x;
            pixel[1] = color.y;
            pixel[2] = color.z;
            pixel[3] = color.w;
        }
    }
    return true;
}

bool Rasterizer::SubmitGpuSurfaceTransfer(const DkImageView& source, u32 source_width,
                                          u32 source_height,
                                          State::CachedRenderTarget& destination, u32 flags) {
    FrameContext& context = CurrentFrameContext();
    if (!WaitForFrameContext(context)) {
        return false;
    }
    dkCmdBufClear(command_buffer);
    dkCmdBufAddMemory(command_buffer, command_mem_block, context.command_offset,
                      context.command_size);
    const DkImageRect source_rect{0, 0, 0, source_width, source_height, 1};
    const DkImageRect destination_rect{0, 0, 0, destination.key.width, destination.key.height, 1};
    dkCmdBufBarrier(command_buffer, DkBarrier_Fragments, DkInvalidateFlags_Image);
    dkCmdBufBlitImage(command_buffer, &source, &source_rect, &destination.view, &destination_rect,
                      flags | DkBlitFlag_ModeBlit, 0);
    dkCmdBufSignalFence(command_buffer, &context.fence, true);
    const DkCmdList list = dkCmdBufFinishList(command_buffer);
    if (!list) {
        return false;
    }
    dkQueueSubmitCommands(queue, list);
    ++submissions_since_flush;
    context.fence_pending = true;
    context.pending_vertices = 0;
    state.MarkRenderTargetGpuDirty(destination);
    state.SelectPresentRenderTarget(destination.key.color_address);
    FlushQueueIfNeeded(false);
    return !QueueHasError("after GPU surface transfer");
}

bool Rasterizer::SubmitGuestMemoryTransfer(const Pica::DisplayTransferConfig& config,
                                           TransferSource& source,
                                           State::CachedRenderTarget& destination, u32 flags) {
    const u64 upload_bytes_64 =
        static_cast<u64>(source.key.width) * source.key.height * 4;
    if (upload_bytes_64 == 0 || upload_bytes_64 > std::numeric_limits<u32>::max()) {
        return false;
    }
    const u32 upload_bytes = static_cast<u32>(upload_bytes_64);
    TransferUploadContext* const upload = AcquireTransferUploadContext(upload_bytes);
    if (!upload || !DecodeTransferSource(config, upload->cpu, upload->capacity)) {
        return false;
    }
    FrameContext& context = CurrentFrameContext();
    if (!WaitForFrameContext(context)) {
        return false;
    }

    dkCmdBufClear(command_buffer);
    dkCmdBufAddMemory(command_buffer, command_mem_block, context.command_offset,
                      context.command_size);
    const DkCopyBuf upload_source{upload->gpu, source.key.width, source.key.height};
    const DkImageRect source_destination{0, 0, 0, source.key.width, source.key.height, 1};
    dkCmdBufCopyBufferToImage(command_buffer, &upload_source, &source.view, &source_destination, 0);
    dkCmdBufBarrier(command_buffer, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_L2Cache);
    const DkImageRect source_rect{0, 0, 0, source.key.width, source.key.height, 1};
    const DkImageRect destination_rect{0, 0, 0, destination.key.width, destination.key.height, 1};
    dkCmdBufBlitImage(command_buffer, &source.view, &source_rect, &destination.view,
                      &destination_rect, flags | DkBlitFlag_ModeBlit, 0);
    dkCmdBufSignalFence(command_buffer, &upload->fence, true);
    dkCmdBufSignalFence(command_buffer, &context.fence, true);
    const DkCmdList list = dkCmdBufFinishList(command_buffer);
    if (!list) {
        return false;
    }
    dkQueueSubmitCommands(queue, list);
    ++submissions_since_flush;
    upload->fence_pending = true;
    context.fence_pending = true;
    context.pending_vertices = 0;
    state.MarkRenderTargetGpuDirty(destination);
    state.SelectPresentRenderTarget(destination.key.color_address);
    FlushQueueIfNeeded(false);
    return !QueueHasError("after guest-memory display transfer");
}
#endif

bool Rasterizer::AccelerateDisplayTransfer(const Pica::DisplayTransferConfig& config) {
#ifdef __SWITCH__
    if (!initialized || config.input_width == 0 || config.input_height == 0 ||
        config.output_width == 0 || config.output_height == 0) {
        return false;
    }
    const u32 horizontal_shift = config.scaling != Pica::DisplayTransferConfig::NoScale ? 1 : 0;
    const u32 vertical_shift = config.scaling == Pica::DisplayTransferConfig::ScaleXY ? 1 : 0;
    const u32 output_width = config.output_width.Value() >> horizontal_shift;
    const u32 output_height = config.output_height.Value() >> vertical_shift;
    if (output_width == 0 || output_height == 0) {
        return false;
    }
    State::RenderTargetKey destination_key{
        .color_address = config.GetPhysicalOutputAddress(),
        .width = output_width,
        .height = output_height,
        .format = CanonicalColorFormat(config.output_format.Value()),
    };
    State::CachedRenderTarget* const destination = state.GetOrCreateRenderTarget(destination_key);
    if (!destination) {
        return false;
    }
    u32 flags = config.flip_vertically ? DkBlitFlag_FlipY : 0;
    flags |= config.scaling == Pica::DisplayTransferConfig::NoScale ? DkBlitFlag_FilterNearest
                                                                    : DkBlitFlag_FilterLinear;

    bool completed = false;
    if (const auto* const cached_source =
            state.FindGpuDirtyRenderTarget(config.GetPhysicalInputAddress())) {
        completed = SubmitGpuSurfaceTransfer(cached_source->view, cached_source->key.width,
                                             cached_source->key.height, *destination, flags);
    } else if (TransferSource* const source = GetOrCreateTransferSource(config)) {
        completed = SubmitGuestMemoryTransfer(config, *source, *destination, flags);
    }
    RecordPicaDisplayTransfer(static_cast<u64>(config.input_width.Value()) *
                                  config.input_height.Value() *
                                  Pica::BytesPerPixel(config.input_format.Value()),
                              completed);
    return completed;
#else
    (void)config;
    return false;
#endif
}

bool Rasterizer::AccelerateTextureCopy(const Pica::DisplayTransferConfig& config) {
#ifdef __SWITCH__
    // TextureCopy is a byte-oriented PICA DMA operation. Handle it directly in the native
    // rasterizer so GPU.cpp never invokes SwBlitter. Rendering and texture sampling remain Deko3D.
    const PAddr source_address = config.GetPhysicalInputAddress();
    const PAddr destination_address = config.GetPhysicalOutputAddress();
    u8* source = memory.GetPhysicalPointer(source_address);
    u8* destination = memory.GetPhysicalPointer(destination_address);
    u32 remaining = Common::AlignDown(config.texture_copy.size, 16U);
    if (!source || !destination || remaining == 0) {
        return false;
    }
    FlushQueueIfNeeded(true);
    const u32 input_gap = config.texture_copy.input_gap.Value() * 16;
    const u32 output_gap = config.texture_copy.output_gap.Value() * 16;
    const u32 input_width = input_gap == 0 ? remaining : config.texture_copy.input_width.Value() * 16;
    const u32 output_width =
        output_gap == 0 ? remaining : config.texture_copy.output_width.Value() * 16;
    if (input_width == 0 || output_width == 0) {
        return false;
    }
    u32 input_remaining = input_width;
    u32 output_remaining = output_width;
    while (remaining != 0) {
        const u32 amount = std::min({input_remaining, output_remaining, remaining});
        std::memcpy(destination, source, amount);
        source += amount;
        destination += amount;
        input_remaining -= amount;
        output_remaining -= amount;
        remaining -= amount;
        if (input_remaining == 0) {
            input_remaining = input_width;
            source += input_gap;
        }
        if (output_remaining == 0) {
            output_remaining = output_width;
            destination += output_gap;
        }
    }
    texture_cache.InvalidateRegion(destination_address, config.texture_copy.size);
    state.InvalidateRenderTargetsOverlapping(destination_address, config.texture_copy.size,
                                              State::SurfaceOwner::CpuMemory);
    RecordPicaTextureCopy(config.texture_copy.size, true);
    return true;
#else
    (void)config;
    return false;
#endif
}

bool Rasterizer::AccelerateFill(const Pica::MemoryFillConfig& config) {
#ifdef __SWITCH__
    const PAddr start = config.GetStartAddress();
    const PAddr end = config.GetEndAddress();
    if (start == 0 || end <= start) {
        return false;
    }
    if (const auto* const cached = state.FindGpuDirtyRenderTarget(start)) {
        State::CachedRenderTarget* const target = state.GetOrCreateRenderTarget(cached->key);
        FrameContext& context = CurrentFrameContext();
        if (!target || !WaitForFrameContext(context)) {
            return false;
        }
        const u32 value = config.value_32bit;
        const float r = (value & 0xFFU) / 255.0f;
        const float g = ((value >> 8) & 0xFFU) / 255.0f;
        const float b = ((value >> 16) & 0xFFU) / 255.0f;
        const float a = config.fill_32bit ? ((value >> 24) & 0xFFU) / 255.0f : 1.0f;
        dkCmdBufClear(command_buffer);
        dkCmdBufAddMemory(command_buffer, command_mem_block, context.command_offset,
                          context.command_size);
        dkCmdBufBindRenderTarget(command_buffer, &target->view, nullptr);
        dkCmdBufClearColorFloat(command_buffer, 0, DkColorMask_RGBA, r, g, b, a);
        dkCmdBufSignalFence(command_buffer, &context.fence, true);
        const DkCmdList list = dkCmdBufFinishList(command_buffer);
        if (!list) {
            return false;
        }
        dkQueueSubmitCommands(queue, list);
        ++submissions_since_flush;
        context.fence_pending = true;
        context.pending_vertices = 0;
        state.MarkRenderTargetGpuDirty(*target);
        FlushQueueIfNeeded(false);
        RecordPicaMemoryFill(end - start, true);
        return !QueueHasError("after native render-target fill");
    }

    // Non-surface fills are guest-memory DMA, not triangle rasterization. Execute them directly
    // here and keep the software renderer entirely absent from the Switch build.
    u8* const destination = memory.GetPhysicalPointer(start);
    if (!destination) {
        return false;
    }
    const u32 bytes = end - start;
    if (config.fill_24bit) {
        for (u32 offset = 0; offset + 2 < bytes; offset += 3) {
            destination[offset + 0] = config.value_24bit_r.Value();
            destination[offset + 1] = config.value_24bit_g.Value();
            destination[offset + 2] = config.value_24bit_b.Value();
        }
    } else if (config.fill_32bit) {
        const u32 value = config.value_32bit;
        for (u32 offset = 0; offset + 3 < bytes; offset += 4) {
            std::memcpy(destination + offset, &value, sizeof(value));
        }
    } else {
        const u16 value = static_cast<u16>(config.value_16bit.Value());
        for (u32 offset = 0; offset + 1 < bytes; offset += 2) {
            std::memcpy(destination + offset, &value, sizeof(value));
        }
    }
    texture_cache.InvalidateRegion(start, bytes);
    state.InvalidateRenderTargetsOverlapping(start, bytes, State::SurfaceOwner::CpuMemory);
    RecordPicaMemoryFill(bytes, true);
    return true;
#else
    (void)config;
    return false;
#endif
}

} // namespace VideoCore::Deko3D
