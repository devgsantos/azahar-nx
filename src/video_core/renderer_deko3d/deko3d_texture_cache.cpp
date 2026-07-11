// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

#include <algorithm>
#include <cstring>
#include <limits>

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

u32 TextureSourceBytes(u32 width, u32 height, Pica::TexturingRegs::TextureFormat format) {
    using TextureFormat = Pica::TexturingRegs::TextureFormat;
    switch (format) {
    case TextureFormat::RGBA8:
    case TextureFormat::RGB8:
    case TextureFormat::RGB5A1:
    case TextureFormat::RGB565:
    case TextureFormat::RGBA4:
    case TextureFormat::IA8:
    case TextureFormat::RG8:
    case TextureFormat::I8:
    case TextureFormat::A8:
    case TextureFormat::IA4:
    case TextureFormat::I4:
    case TextureFormat::A4: {
        const u64 nibbles = static_cast<u64>(width) * height *
                            Pica::TexturingRegs::NibblesPerPixel(format);
        return static_cast<u32>((nibbles + 1) / 2);
    }
    case TextureFormat::ETC1:
    case TextureFormat::ETC1A4: {
        const u32 tiles_x = std::max(1u, (width + 7u) / 8u);
        const u32 tiles_y = std::max(1u, (height + 7u) / 8u);
        const u64 bytes = static_cast<u64>(tiles_x) * tiles_y *
                          Pica::Texture::CalculateTileSize(format);
        return bytes > std::numeric_limits<u32>::max() ? 0 : static_cast<u32>(bytes);
    }
    }
    return 0;
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

TextureCache::~TextureCache() {
    Shutdown();
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
        return it->second.get();
    }

    if (!MapTextureFormat(config.format)) {
        return nullptr;
    }

    auto cached = std::make_unique<CachedTexture>();
    cached->physical_address = config.config.GetPhysicalAddress();
    cached->source_bytes = TextureSourceBytes(config.config.width, config.config.height, config.format);
    cached->width = config.config.width;
    cached->height = config.config.height;
    cached->format = config.format;

    if (!AllocateTexture(*cached, cached->width, cached->height, config.format)) {
        return nullptr;
    }

    if (!UploadTexture(*cached, config.config, config.format)) {
        DestroyTexture(*cached);
        return nullptr;
    }
    cached->sampler = CreateSampler(config.config);

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
    if (image_size == 0 || image_alignment == 0 ||
        image_size > std::numeric_limits<u32>::max()) {
        return false;
    }
    const u32 image_bytes = AlignUp(static_cast<u32>(image_size), image_alignment);
    const u32 allocation_bytes = AlignUp(image_bytes, DK_MEMBLOCK_ALIGNMENT);

    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device, allocation_bytes);
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    cached.mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!cached.mem_block) {
        return false;
    }

    dkImageInitialize(&cached.image, &layout, cached.mem_block, 0);
    dkImageViewDefaults(&cached.view, &cached.image);
    return true;
}

bool TextureCache::UploadTexture(CachedTexture& cached,
                                 const Pica::TexturingRegs::TextureConfig& config,
                                 Pica::TexturingRegs::TextureFormat format) {
    if (!memory) {
        return false;
    }

    const u32 width = config.width;
    const u32 height = config.height;
    static bool s_logged_first_upload = false;
    if (!s_logged_first_upload) {
        s_logged_first_upload = true;
        LOG_INFO(Render, "Texture upload begin addr=0x{:08x} size={}x{} format={}",
                 config.GetPhysicalAddress(), width, height, static_cast<u32>(format));
    }
    const u32 linear_stride = width * 4;
    const u32 required_size = linear_stride * height;
    if (required_size > StagingBufferSize) {
        LOG_WARNING(Render,
                    "Deko3D texture cache: skipping upload of {}x{} texture ({} bytes needed, "
                    "{} available)",
                    width, height, required_size, StagingBufferSize);
        return false;
    }

    const u8* texture_data = memory->GetPhysicalPointer(config.GetPhysicalAddress());
    if (!texture_data) {
        LOG_WARNING(Render, "Deko3D texture cache: guest texture memory not mapped at 0x{:08x}",
                    config.GetPhysicalAddress());
        return false;
    }

    const auto info = Pica::Texture::TextureInfo::FromPicaRegister(config, format);
    auto* const staging_pixels = static_cast<u8*>(staging_cpu_addr);
    static bool s_logged_first_texture_samples = false;
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
    if (!s_logged_first_texture_samples) {
        s_logged_first_texture_samples = true;
        const auto sample_tl = Pica::Texture::LookupTexture(texture_data, 0, height - 1, info);
        const auto sample_center =
            Pica::Texture::LookupTexture(texture_data, width / 2, height / 2, info);
        const auto sample_br = Pica::Texture::LookupTexture(texture_data, width - 1, 0, info);
        LOG_INFO(Render,
                 "Deko3D first texture decoded samples format={} tl=({},{},{},{}) "
                 "center=({},{},{},{}) br=({},{},{},{})",
                 static_cast<u32>(format), sample_tl.r(), sample_tl.g(), sample_tl.b(),
                 sample_tl.a(), sample_center.r(), sample_center.g(), sample_center.b(),
                 sample_center.a(), sample_br.r(), sample_br.g(), sample_br.b(), sample_br.a());
    }

    dkCmdBufClear(upload_command_buffer);
    dkCmdBufAddMemory(upload_command_buffer, upload_command_mem_block, 0, UploadCommandSize);

    DkCopyBuf copy_buf{};
    copy_buf.addr = staging_gpu_addr;
    copy_buf.rowLength = 0;
    copy_buf.imageHeight = 0;

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
        return false;
    }
    dkQueueFlush(state->GetQueue());
    dkQueueSubmitCommands(state->GetQueue(), cmd_list);
    dkQueueWaitIdle(state->GetQueue());
    if (dkQueueIsInErrorState(state->GetQueue())) {
        LOG_ERROR(Render, "Texture upload failed: queue entered error state");
        return false;
    }
    return true;
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
    case Pica::TexturingRegs::TextureFormat::ETC1:
    case Pica::TexturingRegs::TextureFormat::ETC1A4:
        // All source formats are decoded to RGBA8 before upload.
        return DkImageFormat_RGBA8_Unorm;
    }
    return std::nullopt;
}

u64 TextureCache::ComputeTextureKey(const Pica::TexturingRegs::FullTextureConfig& config) const {
    u64 key = static_cast<u64>(config.config.GetPhysicalAddress());
    key ^= static_cast<u64>(config.config.width) << 16;
    key ^= static_cast<u64>(config.config.height) << 32;
    key ^= static_cast<u64>(static_cast<u32>(config.format)) << 48;
    key ^= static_cast<u64>(config.config.type.Value()) << 56;
    key ^= static_cast<u64>(config.config.wrap_s.Value()) << 4;
    key ^= static_cast<u64>(config.config.wrap_t.Value()) << 7;
    key ^= static_cast<u64>(config.config.min_filter.Value()) << 10;
    key ^= static_cast<u64>(config.config.mag_filter.Value()) << 11;
    key ^= static_cast<u64>(config.config.mip_filter.Value()) << 12;
    key ^= static_cast<u64>(config.config.border_color.raw) << 20;
    key ^= static_cast<u64>(config.config.lod.max_level.Value()) << 52;
    key ^= static_cast<u64>(config.config.lod.min_level.Value()) << 56;
    return key;
}

void TextureCache::InvalidateRegion(PAddr address, u32 size) {
    if (address == 0 || size == 0) {
        return;
    }
    for (auto it = cache.begin(); it != cache.end();) {
        CachedTexture& texture = *it->second;
        if (!RangesOverlap(address, size, texture.physical_address, texture.source_bytes)) {
            ++it;
            continue;
        }
        DestroyTexture(texture);
        it = cache.erase(it);
    }
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
