// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <limits>

#include "common/alignment.h"
#include "common/assert.h"
#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "core/memory.h"
#include "switch/switch_debug_log.h"
#include "video_core/pica/regs_texturing.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/texture/texture_decode.h"

namespace VideoCore::Deko3D {

#ifdef __SWITCH__
namespace {

constexpr u32 StagingBufferSize = 8 * 1024 * 1024;
constexpr u32 TextureTileWidth = 8;
constexpr u32 TextureTileHeight = 8;
constexpr s64 UploadFenceTimeoutNs = 1'000'000'000LL;

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
    static_assert(StagingBufferSize == UploadSlotCount * UploadStagingSliceSize);

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
    if (!staging_cpu_addr || staging_gpu_addr == 0) {
        LOG_ERROR(Render, "Deko3D texture cache staging buffer mapping failed");
        Shutdown();
        return false;
    }

    for (u32 index = 0; index < UploadSlotCount; ++index) {
        auto& slot = upload_slots[index];
        slot.staging_offset = index * UploadStagingSliceSize;

        DkMemBlockMaker command_maker;
        dkMemBlockMakerDefaults(&command_maker, device, UploadCommandSize);
        command_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
        slot.command_mem_block = dkMemBlockCreate(&command_maker);
        if (!slot.command_mem_block) {
            LOG_ERROR(Render, "Deko3D texture upload command memory allocation failed for slot {}",
                      index);
            Shutdown();
            return false;
        }

        DkCmdBufMaker command_buffer_maker;
        dkCmdBufMakerDefaults(&command_buffer_maker, device);
        slot.command_buffer = dkCmdBufCreate(&command_buffer_maker);
        if (!slot.command_buffer) {
            LOG_ERROR(Render, "Deko3D texture upload command buffer creation failed for slot {}",
                      index);
            Shutdown();
            return false;
        }
        dkCmdBufAddMemory(slot.command_buffer, slot.command_mem_block, 0, UploadCommandSize);
    }

    LOG_INFO(Render,
             "Deko3D texture cache initialized with {} upload slots, {} staging bytes and {} cache bytes",
             UploadSlotCount, StagingBufferSize, TextureCacheBudgetBytes);
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
    const bool uploads_pending = std::any_of(upload_slots.begin(), upload_slots.end(),
                                             [](const UploadSlot& slot) {
                                                 return slot.fence_pending;
                                             });
    if (uploads_pending && state) {
        state->WaitIdle();
    }
    for (auto& slot : upload_slots) {
        slot.fence_pending = false;
    }

    for (auto& [key, cached] : cache) {
        DestroyTexture(*cached);
    }
    cache.clear();
    cache_allocation_bytes = 0;
    cache_generation = 0;

    for (auto& slot : upload_slots) {
        if (slot.command_buffer) {
            dkCmdBufDestroy(slot.command_buffer);
            slot.command_buffer = nullptr;
        }
        if (slot.command_mem_block) {
            dkMemBlockDestroy(slot.command_mem_block);
            slot.command_mem_block = nullptr;
        }
        slot = {};
    }

    if (staging_mem_block) {
        dkMemBlockDestroy(staging_mem_block);
        staging_mem_block = nullptr;
    }
    staging_gpu_addr = 0;
    staging_cpu_addr = nullptr;
    current_upload_slot = 0;
    next_upload_serial = 0;
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

    if (config.config.type != Pica::TexturingRegs::TextureConfig::Texture2D) {
        return nullptr;
    }

    const u64 key = ComputeTextureKey(config);
    auto it = cache.find(key);
    if (it != cache.end()) {
        auto& cached = *it->second;
        cached.last_used_generation = ++cache_generation;
        if (cache_generation == 0) {
            cache_generation = 1;
            cached.last_used_generation = cache_generation;
        }
        return &cached;
    }

    if (!MapTextureFormat(config.format)) {
        return nullptr;
    }

    auto cached = std::make_unique<CachedTexture>();
    cached->physical_address = config.config.GetPhysicalAddress();
    cached->source_bytes =
        TextureSourceBytes(config.config.width, config.config.height, config.format);
    cached->width = config.config.width;
    cached->height = config.config.height;
    cached->format = config.format;

    if (!AllocateTexture(*cached, cached->width, cached->height, config.format)) {
        return nullptr;
    }
    if (!EvictForAllocation(cached->allocation_bytes)) {
        DestroyTexture(*cached);
        return nullptr;
    }
    if (!UploadTexture(*cached, config.config, config.format)) {
        DestroyTexture(*cached);
        return nullptr;
    }
    cached->sampler = CreateSampler(config.config);
    cached->last_used_generation = ++cache_generation;
    if (cache_generation == 0) {
        cache_generation = 1;
        cached->last_used_generation = cache_generation;
    }

    const CachedTexture* result = cached.get();
    cache_allocation_bytes += cached->allocation_bytes;
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

    cached.allocation_bytes = AlignUp(static_cast<u32>(image_size), image_alignment);
    DkMemBlockMaker mem_block_maker;
    dkMemBlockMakerDefaults(&mem_block_maker, device,
                            static_cast<u32>(cached.allocation_bytes));
    mem_block_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    cached.mem_block = dkMemBlockCreate(&mem_block_maker);
    if (!cached.mem_block) {
        return false;
    }

    dkImageInitialize(&cached.image, &layout, cached.mem_block, 0);
    dkImageViewDefaults(&cached.view, &cached.image);
    return true;
}

bool TextureCache::WaitForUploadSlot(UploadSlot& slot) {
    if (!slot.fence_pending) {
        return true;
    }

    const DkResult poll_result = dkFenceWait(&slot.fence, 0);
    if (poll_result != DkResult_Success) {
        const auto wait_start = std::chrono::steady_clock::now();
        const DkResult wait_result = dkFenceWait(&slot.fence, UploadFenceTimeoutNs);
        const auto wait_end = std::chrono::steady_clock::now();
        if (wait_result != DkResult_Success) {
            const auto wait_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(wait_end - wait_start).count();
            LOG_ERROR(Render, "Deko3D texture upload fence wait failed result={} wait_ms={}",
                      static_cast<int>(wait_result), wait_ms);
            return false;
        }
    }

    slot.fence_pending = false;
    return true;
}

bool TextureCache::WaitForTextureUpload(CachedTexture& cached) {
    if (cached.upload_slot >= UploadSlotCount || cached.upload_serial == 0) {
        return true;
    }

    auto& slot = upload_slots[cached.upload_slot];
    if (slot.serial == cached.upload_serial && slot.fence_pending && !WaitForUploadSlot(slot)) {
        return false;
    }

    cached.upload_slot = 0xFFFFFFFFu;
    cached.upload_serial = 0;
    return true;
}

bool TextureCache::EvictForAllocation(u64 required_bytes) {
    if (required_bytes > TextureCacheBudgetBytes) {
        return false;
    }

    while (cache_allocation_bytes + required_bytes > TextureCacheBudgetBytes && !cache.empty()) {
        auto victim = std::min_element(
            cache.begin(), cache.end(), [](const auto& lhs, const auto& rhs) {
                return lhs.second->last_used_generation < rhs.second->last_used_generation;
            });
        if (victim == cache.end()) {
            return false;
        }

        CachedTexture& texture = *victim->second;
        const u64 victim_bytes = texture.allocation_bytes;
        DestroyTexture(texture);
        cache.erase(victim);
        cache_allocation_bytes = victim_bytes > cache_allocation_bytes
                                     ? 0
                                     : cache_allocation_bytes - victim_bytes;
    }
    return cache_allocation_bytes + required_bytes <= TextureCacheBudgetBytes;
}

bool TextureCache::UploadTexture(CachedTexture& cached,
                                 const Pica::TexturingRegs::TextureConfig& config,
                                 Pica::TexturingRegs::TextureFormat format) {
    if (!memory || !state) {
        return false;
    }

    const u32 width = config.width;
    const u32 height = config.height;
    const u32 required_size = width * height * 4;
    if (required_size > UploadStagingSliceSize) {
        LOG_WARNING(Render,
                    "Deko3D texture cache: skipping upload of {}x{} texture ({} bytes needed, "
                    "{} available per asynchronous slot)",
                    width, height, required_size, UploadStagingSliceSize);
        return false;
    }

    const u8* texture_data = memory->GetPhysicalPointer(config.GetPhysicalAddress());
    if (!texture_data) {
        LOG_WARNING(Render, "Deko3D texture cache: guest texture memory not mapped at 0x{:08x}",
                    config.GetPhysicalAddress());
        return false;
    }

    const u32 slot_index = current_upload_slot;
    current_upload_slot = (current_upload_slot + 1) % UploadSlotCount;
    auto& upload_slot = upload_slots[slot_index];
    if (!WaitForUploadSlot(upload_slot)) {
        return false;
    }

    const auto info = Pica::Texture::TextureInfo::FromPicaRegister(config, format);
    auto* const staging_pixels =
        static_cast<u8*>(staging_cpu_addr) + upload_slot.staging_offset;
    const std::size_t tile_size = Pica::Texture::CalculateTileSize(format);
    const u32 tiles_x = std::max(1u, (width + TextureTileWidth - 1) / TextureTileWidth);
    const u32 tiles_y = std::max(1u, (height + TextureTileHeight - 1) / TextureTileHeight);
    const std::size_t tile_row_stride =
        info.stride > 0 ? static_cast<std::size_t>(info.stride) : tile_size * tiles_x;

    for (u32 tile_y = 0; tile_y < tiles_y; ++tile_y) {
        const u8* const tile_row = texture_data + static_cast<std::size_t>(tile_y) * tile_row_stride;
        for (u32 tile_x = 0; tile_x < tiles_x; ++tile_x) {
            const u8* const tile = tile_row + static_cast<std::size_t>(tile_x) * tile_size;
            for (u32 fine_y = 0; fine_y < TextureTileHeight; ++fine_y) {
                const u32 source_y = tile_y * TextureTileHeight + fine_y;
                if (source_y >= height) {
                    break;
                }
                const u32 destination_y = height - 1 - source_y;
                for (u32 fine_x = 0; fine_x < TextureTileWidth; ++fine_x) {
                    const u32 x = tile_x * TextureTileWidth + fine_x;
                    if (x >= width) {
                        break;
                    }
                    const auto color =
                        Pica::Texture::LookupTexelInTile(tile, fine_x, fine_y, info, false);
                    const std::size_t dst =
                        (static_cast<std::size_t>(destination_y) * width + x) * 4;
                    staging_pixels[dst + 0] = color.r();
                    staging_pixels[dst + 1] = color.g();
                    staging_pixels[dst + 2] = color.b();
                    staging_pixels[dst + 3] = color.a();
                }
            }
        }
    }

    DkCmdBuf command_buffer = upload_slot.command_buffer;
    dkCmdBufClear(command_buffer);
    dkCmdBufAddMemory(command_buffer, upload_slot.command_mem_block, 0, UploadCommandSize);

    DkCopyBuf copy_buf{};
    copy_buf.addr = staging_gpu_addr + upload_slot.staging_offset;
    copy_buf.rowLength = 0;
    copy_buf.imageHeight = 0;

    DkImageRect dst_rect{};
    dst_rect.x = 0;
    dst_rect.y = 0;
    dst_rect.z = 0;
    dst_rect.width = width;
    dst_rect.height = height;
    dst_rect.depth = 1;

    dkCmdBufCopyBufferToImage(command_buffer, &copy_buf, &cached.view, &dst_rect, 0);
    dkCmdBufSignalFence(command_buffer, &upload_slot.fence, true);

    const DkCmdList command_list = dkCmdBufFinishList(command_buffer);
    if (!command_list) {
        LOG_WARNING(Render, "Deko3D texture cache: failed to finish upload command list");
        return false;
    }

    upload_slot.serial = ++next_upload_serial;
    if (upload_slot.serial == 0) {
        upload_slot.serial = ++next_upload_serial;
    }
    upload_slot.fence_pending = true;
    cached.upload_slot = slot_index;
    cached.upload_serial = upload_slot.serial;

    dkQueueSubmitCommands(state->GetQueue(), command_list);
    dkQueueFlush(state->GetQueue());
    if (dkQueueIsInErrorState(state->GetQueue())) {
        LOG_ERROR(Render, "Deko3D texture upload failed: queue entered error state");
        return false;
    }
    return true;
}

void TextureCache::DestroyTexture(CachedTexture& cached) {
    if (!WaitForTextureUpload(cached)) {
        if (state) {
            state->WaitIdle();
        }
        for (auto& slot : upload_slots) {
            slot.fence_pending = false;
        }
    }

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
        const u64 allocation_bytes = texture.allocation_bytes;
        DestroyTexture(texture);
        it = cache.erase(it);
        cache_allocation_bytes = allocation_bytes > cache_allocation_bytes
                                     ? 0
                                     : cache_allocation_bytes - allocation_bytes;
    }
}

void TextureCache::FlushRegion(PAddr address, u32 size) {
    (void)address;
    (void)size;
}

void TextureCache::FlushAndInvalidateRegion(PAddr address, u32 size) {
    InvalidateRegion(address, size);
}

#endif

} // namespace VideoCore::Deko3D
