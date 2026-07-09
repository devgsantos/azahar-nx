// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

#include <cstring>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "core/memory.h"
#include "video_core/pica/regs_texturing.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/texture/texture_decode.h"

namespace VideoCore::Deko3D {

#ifdef __SWITCH__
namespace {

constexpr u32 StagingBufferSize = 8 * 1024 * 1024; // 8 MiB scratch for uploads
constexpr u32 UploadCommandSize = 16 * 1024;

u32 AlignUp(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

DkWrapMode MapWrapMode(Pica::TexturingRegs::TextureConfig::WrapMode mode) {
    switch (mode) {
    case Pica::TexturingRegs::TextureConfig::ClampToEdge:
    case Pica::TexturingRegs::TextureConfig::ClampToEdge2:
        return DkWrapMode_ClampToEdge;
    case Pica::TexturingRegs::TextureConfig::ClampToBorder:
    case Pica::TexturingRegs::TextureConfig::ClampToBorder2:
        return DkWrapMode_ClampToBorder;
    case Pica::TexturingRegs::TextureConfig::Repeat:
    case Pica::TexturingRegs::TextureConfig::Repeat2:
    case Pica::TexturingRegs::TextureConfig::Repeat3:
        return DkWrapMode_Repeat;
    case Pica::TexturingRegs::TextureConfig::MirroredRepeat:
        return DkWrapMode_MirroredRepeat;
    }
    return DkWrapMode_Repeat;
}

DkFilter MapFilter(Pica::TexturingRegs::TextureConfig::TextureFilter filter) {
    switch (filter) {
    case Pica::TexturingRegs::TextureConfig::Nearest:
        return DkFilter_Nearest;
    case Pica::TexturingRegs::TextureConfig::Linear:
        return DkFilter_Linear;
    }
    return DkFilter_Nearest;
}

} // namespace
#endif

bool TextureCache::Initialize(State& state_, Memory::MemorySystem& memory_) {
    SWITCH_TRACE_EVENT("Deko3D", "TextureCache::Initialize", "enter");
#ifdef __SWITCH__
    state = &state_;
    memory = &memory_;
    device = state_.GetDevice();
    if (!device) {
        LOG_ERROR(Render, "Deko3D texture cache cannot initialize without a device");
        return false;
    }

    DkMemBlockMaker staging_maker;
    dkMemBlockMakerDefaults(&staging_maker, device, StagingBufferSize);
    staging_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    staging_mem_block = dkMemBlockCreate(&staging_maker);
    if (!staging_mem_block) {
        LOG_ERROR(Render, "Deko3D texture cache staging buffer allocation failed");
        return false;
    }
    staging_gpu_addr = dkMemBlockGetGpuAddr(staging_mem_block);
    staging_cpu_addr = dkMemBlockGetCpuAddr(staging_mem_block);

    DkMemBlockMaker cmd_maker;
    dkMemBlockMakerDefaults(&cmd_maker, device, UploadCommandSize);
    cmd_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    upload_command_mem_block = dkMemBlockCreate(&cmd_maker);
    if (!upload_command_mem_block) {
        LOG_ERROR(Render, "Deko3D texture cache upload command buffer allocation failed");
        return false;
    }

    DkCmdBufMaker cmd_buf_maker;
    dkCmdBufMakerDefaults(&cmd_buf_maker, device);
    upload_command_buffer = dkCmdBufCreate(&cmd_buf_maker);
    if (!upload_command_buffer) {
        LOG_ERROR(Render, "Deko3D texture cache upload command buffer creation failed");
        return false;
    }
    dkCmdBufAddMemory(upload_command_buffer, upload_command_mem_block, 0, UploadCommandSize);

    LOG_INFO(Render,
             "Deko3D texture cache initialized with {} bytes of staging memory and {} bytes of "
             "upload command memory",
             StagingBufferSize, UploadCommandSize);
#else
    (void)state_;
    (void)memory_;
#endif
    initialized = true;
    SWITCH_TRACE_EVENT("Deko3D", "TextureCache::Initialize", "leave");
    return true;
}

void TextureCache::Shutdown() {
#ifdef __SWITCH__
    for (auto& [key, cached] : cache) {
        DestroyTexture(*cached);
    }
    cache.clear();

    if (upload_command_buffer) {
        dkCmdBufDestroy(upload_command_buffer);
        upload_command_buffer = nullptr;
    }
    if (upload_command_mem_block) {
        dkMemBlockDestroy(upload_command_mem_block);
        upload_command_mem_block = nullptr;
    }
    if (staging_mem_block) {
        dkMemBlockDestroy(staging_mem_block);
        staging_mem_block = nullptr;
    }
    staging_gpu_addr = 0;
    staging_cpu_addr = nullptr;
    device = nullptr;
    state = nullptr;
    memory = nullptr;
#endif
    initialized = false;
}

#ifdef __SWITCH__

const CachedTexture* TextureCache::GetTexture(
    const Pica::TexturingRegs::FullTextureConfig& config) {
    if (!config.enabled || config.config.address == 0 || config.config.width == 0 ||
        config.config.height == 0) {
        return nullptr;
    }

    // Only accelerate 2D textures for now.
    if (config.config.type != Pica::TexturingRegs::TextureConfig::Texture2D) {
        return nullptr;
    }

    const u64 key = ComputeTextureKey(config);
    auto it = cache.find(key);
    if (it != cache.end()) {
        if (it->second->generation == generation) {
            return it->second.get();
        }
        // Cached entry was invalidated; destroy and recreate.
        DestroyTexture(*it->second);
        cache.erase(it);
    }

    if (!MapTextureFormat(config.format)) {
        return nullptr;
    }

    auto cached = std::make_unique<CachedTexture>();
    cached->physical_address = config.config.GetPhysicalAddress();
    cached->width = config.config.width;
    cached->height = config.config.height;
    cached->format = config.format;

    if (!AllocateTexture(*cached, cached->width, cached->height, config.format)) {
        return nullptr;
    }

    UploadTexture(*cached, config.config, config.format);
    cached->sampler = CreateSampler(config.config);
    cached->generation = generation;

    const CachedTexture* result = cached.get();
    cache.emplace(key, std::move(cached));
    return result;
}

bool TextureCache::AllocateTexture(CachedTexture& cached, u32 width, u32 height,
                                   Pica::TexturingRegs::TextureFormat format) {
    const auto mapped_format = MapTextureFormat(format);
    if (!mapped_format) {
        return false;
    }

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_Usage2DEngine;
    layout_maker.format = *mapped_format;
    layout_maker.dimensions[0] = width;
    layout_maker.dimensions[1] = height;
    // Work around a hardware quirk with very short textures (see deko3d issue #10).
    if (height <= 8) {
        layout_maker.flags |= DkImageFlags_CustomTileSize;
        layout_maker.tileSize = DkTileSize_OneGob;
    }

    DkImageLayout layout;
    dkImageLayoutInitialize(&layout, &layout_maker);
    const u64 image_size = dkImageLayoutGetSize(&layout);
    const u32 image_alignment = dkImageLayoutGetAlignment(&layout);
    if (image_size == 0 || image_alignment == 0 || image_size > std::numeric_limits<u32>::max()) {
        return false;
    }

    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device,
                            static_cast<u32>(AlignUp(image_size, image_alignment)));
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    cached.mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!cached.mem_block) {
        return false;
    }

    dkImageInitialize(&cached.image, &layout, cached.mem_block, 0);
    dkImageViewDefaults(&cached.view, &cached.image);
    return true;
}

void TextureCache::UploadTexture(CachedTexture& cached,
                                 const Pica::TexturingRegs::TextureConfig& config,
                                 Pica::TexturingRegs::TextureFormat format) {
    if (!memory) {
        return;
    }

    const u32 width = config.width;
    const u32 height = config.height;
    const u32 linear_stride = width * 4;
    const u32 required_size = linear_stride * height;
    if (required_size > StagingBufferSize) {
        LOG_WARNING(Render,
                    "Deko3D texture cache: skipping upload of {}x{} texture ({} bytes needed, "
                    "{} available)",
                    width, height, required_size, StagingBufferSize);
        return;
    }

    const u8* texture_data = memory->GetPhysicalPointer(config.GetPhysicalAddress());
    if (!texture_data) {
        LOG_WARNING(Render, "Deko3D texture cache: guest texture memory not mapped at 0x{:08x}",
                    config.GetPhysicalAddress());
        return;
    }

    const auto info = Pica::Texture::TextureInfo::FromPicaRegister(config, format);
    auto* const staging_pixels = static_cast<u8*>(staging_cpu_addr);
    for (u32 y = 0; y < height; ++y) {
        // PICA stores textures bottom-to-top; GPU images are top-to-bottom.
        const u32 src_y = height - 1 - y;
        for (u32 x = 0; x < width; ++x) {
            const auto color = Pica::Texture::LookupTexture(texture_data, x, src_y, info);
            const std::size_t dst = (y * width + x) * 4;
            staging_pixels[dst + 0] = color.r();
            staging_pixels[dst + 1] = color.g();
            staging_pixels[dst + 2] = color.b();
            staging_pixels[dst + 3] = color.a();
        }
    }

    dkCmdBufClear(upload_command_buffer);
    dkCmdBufAddMemory(upload_command_buffer, upload_command_mem_block, 0, UploadCommandSize);

    DkCopyBuf copy_buf{};
    copy_buf.addr = staging_gpu_addr;
    copy_buf.rowLength = width;
    copy_buf.imageHeight = height;

    DkImageRect dst_rect{};
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.z = 0;
    dst_rect.width = width;
    dst_rect.height = height;
    dst_rect.depth = 1;

    dkCmdBufCopyBufferToImage(upload_command_buffer, &copy_buf, &cached.view, &dst_rect, 0);

    const DkCmdList cmd_list = dkCmdBufFinishList(upload_command_buffer);
    if (!cmd_list) {
        LOG_WARNING(Render, "Deko3D texture cache: failed to finish upload command list");
        return;
    }
    dkQueueSubmitCommands(state->GetQueue(), cmd_list);
    dkQueueWaitIdle(state->GetQueue());
}

void TextureCache::DestroyTexture(CachedTexture& cached) {
    cached.view = {};
    cached.image = {};
    if (cached.mem_block) {
        dkMemBlockDestroy(cached.mem_block);
        cached.mem_block = nullptr;
    }
}

DkSampler TextureCache::CreateSampler(const Pica::TexturingRegs::TextureConfig& config) const {
    DkSampler sampler{};
    dkSamplerDefaults(&sampler);
    sampler.magFilter = MapFilter(config.mag_filter.Value());
    sampler.minFilter = MapFilter(config.min_filter.Value());
    sampler.mipFilter =
        config.mip_filter.Value() == Pica::TexturingRegs::TextureConfig::Linear
            ? DkMipFilter_Linear
            : DkMipFilter_Nearest;
    sampler.wrapMode[0] = MapWrapMode(config.wrap_s.Value());
    sampler.wrapMode[1] = MapWrapMode(config.wrap_t.Value());
    sampler.wrapMode[2] = DkWrapMode_ClampToEdge;
    return sampler;
}

std::optional<DkImageFormat> TextureCache::MapTextureFormat(
    Pica::TexturingRegs::TextureFormat format) const {
    switch (format) {
    case Pica::TexturingRegs::TextureFormat::RGBA8:
    case Pica::TexturingRegs::TextureFormat::RGB8:
    case Pica::TexturingRegs::TextureFormat::RGB5A1:
    case Pica::TexturingRegs::TextureFormat::RGB565:
    case Pica::TexturingRegs::TextureFormat::RGBA4:
    case Pica::TexturingRegs::TextureFormat::IA8:
    case Pica::TexturingRegs::TextureFormat::RG8:
    case Pica::TexturingRegs::TextureFormat::I8:
    case Pica::TexturingRegs::TextureFormat::A8:
    case Pica::TexturingRegs::TextureFormat::IA4:
    case Pica::TexturingRegs::TextureFormat::I4:
    case Pica::TexturingRegs::TextureFormat::A4:
        // All source formats are decoded to RGBA8 on the CPU before upload.
        return DkImageFormat_RGBA8_Unorm;
    case Pica::TexturingRegs::TextureFormat::ETC1:
    case Pica::TexturingRegs::TextureFormat::ETC1A4:
        // ETC1 would need a dedicated compressed GPU format; fall back for now.
        return std::nullopt;
    }
    return std::nullopt;
}

u64 TextureCache::ComputeTextureKey(const Pica::TexturingRegs::FullTextureConfig& config) const {
    // Pack address, dimensions, and format into a stable key.
    u64 key = static_cast<u64>(config.config.GetPhysicalAddress());
    key ^= static_cast<u64>(config.config.width) << 16;
    key ^= static_cast<u64>(config.config.height) << 32;
    key ^= static_cast<u64>(static_cast<u32>(config.format)) << 48;
    key ^= static_cast<u64>(config.config.type.Value()) << 56;
    return key;
}

void TextureCache::InvalidateRegion(PAddr address, u32 size) {
    if (address == 0 || size == 0) {
        return;
    }
    ++generation;
    (void)address;
    (void)size;
}

void TextureCache::FlushRegion(PAddr address, u32 size) {
    // Texture cache does not write back to guest memory; invalidation is enough.
    InvalidateRegion(address, size);
}

void TextureCache::FlushAndInvalidateRegion(PAddr address, u32 size) {
    InvalidateRegion(address, size);
}

#endif

} // namespace VideoCore::Deko3D
