// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <vector>

#include "common/logging/log.h"
#include "core/memory.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"
#include "video_core/texture/texture_decode.h"

namespace VideoCore::Deko3D {
namespace {

#ifdef __SWITCH__
constexpr u32 UploadCommandMemorySize = 64 * 1024;

u32 AlignUp(u32 value, u32 alignment) {
    return alignment == 0 ? value : ((value + alignment - 1) / alignment) * alignment;
}

DkWrapMode MapWrap(Pica::TexturingRegs::TextureConfig::WrapMode mode) {
    using Wrap = Pica::TexturingRegs::TextureConfig::WrapMode;
    switch (mode) {
    case Wrap::ClampToEdge:
    case Wrap::ClampToEdge2:
        return DkWrapMode_ClampToEdge;
    case Wrap::ClampToBorder:
    case Wrap::ClampToBorder2:
        return DkWrapMode_ClampToBorder;
    case Wrap::MirroredRepeat:
        return DkWrapMode_MirroredRepeat;
    case Wrap::Repeat:
    case Wrap::Repeat2:
    case Wrap::Repeat3:
    default:
        return DkWrapMode_Repeat;
    }
}
#endif

} // namespace

TextureCache::~TextureCache() {
    Shutdown();
}

bool TextureCache::Initialize() {
    initialized = true;
    LOG_INFO(Render, "Deko3D native texture cache initialized");
    return true;
}

bool TextureCache::Attach(State& state_, Memory::MemorySystem& memory_) {
#ifdef __SWITCH__
    state = &state_;
    memory = &memory_;
    device = state->GetDevice();
    queue = state->GetQueue();
    if (!initialized || !device || !queue) {
        return false;
    }
    return InitializeDescriptorStorage() && InitializeUploadResources();
#else
    (void)state_;
    (void)memory_;
    return false;
#endif
}

void TextureCache::Shutdown() {
#ifdef __SWITCH__
    if (state && state->IsInitialized()) {
        state->WaitIdle();
    }
    for (auto& entry : entries) {
        DestroyEntry(*entry);
    }
    entries.clear();
    if (upload_command_buffer) {
        dkCmdBufDestroy(upload_command_buffer);
        upload_command_buffer = nullptr;
    }
    if (upload_command_mem) {
        dkMemBlockDestroy(upload_command_mem);
        upload_command_mem = nullptr;
    }
    if (descriptor_mem) {
        dkMemBlockDestroy(descriptor_mem);
        descriptor_mem = nullptr;
    }
    descriptor_cpu = nullptr;
    descriptor_gpu = 0;
    image_descriptor_gpu_addr = 0;
    sampler_descriptor_gpu_addr = 0;
    device = nullptr;
    queue = nullptr;
    state = nullptr;
    memory = nullptr;
#endif
    initialized = false;
}

#ifdef __SWITCH__
bool TextureCache::InitializeDescriptorStorage() {
    const u32 descriptor_bytes = descriptor_capacity * sizeof(DkImageDescriptor);
    const u32 sampler_bytes = descriptor_capacity * sizeof(DkSamplerDescriptor);
    const u32 total_bytes = AlignUp(descriptor_bytes + sampler_bytes, DK_MEMBLOCK_ALIGNMENT);

    DkMemBlockMaker maker;
    dkMemBlockMakerDefaults(&maker, device, total_bytes);
    maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached | DkMemBlockFlags_ZeroFillInit;
    descriptor_mem = dkMemBlockCreate(&maker);
    if (!descriptor_mem) {
        LOG_ERROR(Render, "Deko3D descriptor memory allocation failed");
        return false;
    }
    descriptor_cpu = dkMemBlockGetCpuAddr(descriptor_mem);
    descriptor_gpu = dkMemBlockGetGpuAddr(descriptor_mem);
    if (!descriptor_cpu || descriptor_gpu == DK_GPU_ADDR_INVALID) {
        LOG_ERROR(Render, "Deko3D descriptor memory mapping failed");
        return false;
    }
    image_descriptor_gpu_addr = descriptor_gpu;
    sampler_descriptor_gpu_addr = descriptor_gpu + descriptor_bytes;
    return true;
}

bool TextureCache::InitializeUploadResources() {
    DkMemBlockMaker mem_maker;
    dkMemBlockMakerDefaults(&mem_maker, device, UploadCommandMemorySize);
    mem_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    upload_command_mem = dkMemBlockCreate(&mem_maker);
    if (!upload_command_mem) {
        return false;
    }
    DkCmdBufMaker cmd_maker;
    dkCmdBufMakerDefaults(&cmd_maker, device);
    upload_command_buffer = dkCmdBufCreate(&cmd_maker);
    return upload_command_buffer != nullptr;
}

TextureCache::BoundTextureSet TextureCache::PrepareTextures(const Pica::TexturingRegs& regs) {
    BoundTextureSet result{};
    if (!initialized || !device || !queue || !memory) {
        result.valid = false;
        return result;
    }

    const auto textures = regs.GetTextures();
    for (u32 unit = 0; unit < textures.size(); ++unit) {
        if (textures[unit].enabled == 0) {
            result.handles[unit] = dkMakeTextureHandle(0, 0);
            continue;
        }
        Entry* const entry = FindOrCreate(textures[unit]);
        if (!entry || entry->invalidated) {
            result.valid = false;
            continue;
        }
        result.enabled_mask |= 1U << unit;
        result.handles[unit] =
            dkMakeTextureHandle(entry->image_descriptor, entry->sampler_descriptor);
    }
    return result;
}

TextureCache::Entry* TextureCache::FindOrCreate(
    const Pica::TexturingRegs::FullTextureConfig& texture) {
    const auto& config = texture.config;
    const u32 sampler_raw = static_cast<u32>(config.mag_filter.Value()) |
                            (static_cast<u32>(config.min_filter.Value()) << 2) |
                            (static_cast<u32>(config.wrap_s.Value()) << 8) |
                            (static_cast<u32>(config.wrap_t.Value()) << 12) |
                            (static_cast<u32>(config.mip_filter.Value()) << 24);
    const u32 lod_raw = (static_cast<u32>(config.lod.bias.Value()) & 0x1FFFU) |
                        (config.lod.max_level.Value() << 16) |
                        (config.lod.min_level.Value() << 24);
    TextureKey key{
        .address = config.GetPhysicalAddress(),
        .width = config.width.Value(),
        .height = config.height.Value(),
        .format = texture.format,
        .sampler_raw = sampler_raw,
        .lod_raw = lod_raw,
    };
    if (key.address == 0 || key.width == 0 || key.height == 0) {
        return nullptr;
    }

    for (auto& entry : entries) {
        if (!entry->invalidated && entry->key == key) {
            RecordTextureCacheHit();
            return entry.get();
        }
    }

    if (next_descriptor >= descriptor_capacity) {
        LOG_ERROR(Render, "Deko3D texture descriptor capacity exhausted");
        return nullptr;
    }

    auto entry = std::make_unique<Entry>();
    entry->key = key;
    entry->image_descriptor = next_descriptor;
    entry->sampler_descriptor = next_descriptor;
    ++next_descriptor;
    if (!UploadEntry(*entry, texture)) {
        DestroyEntry(*entry);
        return nullptr;
    }
    RecordTextureCacheMiss();
    Entry* const result = entry.get();
    entries.emplace_back(std::move(entry));
    return result;
}

bool TextureCache::UploadEntry(Entry& entry,
                               const Pica::TexturingRegs::FullTextureConfig& texture) {
    const Pica::Texture::TextureInfo info =
        Pica::Texture::TextureInfo::FromPicaRegister(texture.config, texture.format);
    const u8* const source = memory->GetPhysicalPointer(info.physical_address);
    if (!source) {
        return false;
    }

    const u64 rgba_bytes_64 = static_cast<u64>(entry.key.width) * entry.key.height * 4;
    if (rgba_bytes_64 == 0 || rgba_bytes_64 > std::numeric_limits<u32>::max()) {
        return false;
    }
    const u32 rgba_bytes = static_cast<u32>(rgba_bytes_64);
    std::vector<u8> decoded(rgba_bytes);
    for (u32 y = 0; y < entry.key.height; ++y) {
        for (u32 x = 0; x < entry.key.width; ++x) {
            const auto color = Pica::Texture::LookupTexture(source, x, y, info);
            u8* const dst = decoded.data() + (static_cast<std::size_t>(y) * entry.key.width + x) * 4;
            dst[0] = color.x;
            dst[1] = color.y;
            dst[2] = color.z;
            dst[3] = color.w;
        }
    }

    DkImageLayoutMaker layout_maker;
    dkImageLayoutMakerDefaults(&layout_maker, device);
    layout_maker.type = DkImageType_2D;
    layout_maker.flags = DkImageFlags_Usage2DEngine;
    layout_maker.format = DkImageFormat_RGBA8_Unorm;
    layout_maker.dimensions[0] = entry.key.width;
    layout_maker.dimensions[1] = entry.key.height;
    DkImageLayout layout{};
    dkImageLayoutInitialize(&layout, &layout_maker);
    const u64 image_size_64 = dkImageLayoutGetSize(&layout);
    const u32 image_alignment = dkImageLayoutGetAlignment(&layout);
    if (image_size_64 == 0 || image_size_64 > std::numeric_limits<u32>::max()) {
        return false;
    }
    const u32 image_size = AlignUp(static_cast<u32>(image_size_64), image_alignment);
    DkMemBlockMaker image_maker;
    dkMemBlockMakerDefaults(&image_maker, device, image_size);
    image_maker.flags = DkMemBlockFlags_GpuCached | DkMemBlockFlags_Image;
    entry.image_mem = dkMemBlockCreate(&image_maker);
    if (!entry.image_mem) {
        return false;
    }
    dkImageInitialize(&entry.image, &layout, entry.image_mem, 0);
    dkImageViewDefaults(&entry.view, &entry.image);

    const u32 staging_size = AlignUp(rgba_bytes, DK_MEMBLOCK_ALIGNMENT);
    DkMemBlockMaker staging_maker;
    dkMemBlockMakerDefaults(&staging_maker, device, staging_size);
    staging_maker.flags = DkMemBlockFlags_CpuUncached | DkMemBlockFlags_GpuCached;
    DkMemBlock staging = dkMemBlockCreate(&staging_maker);
    if (!staging) {
        return false;
    }
    void* const staging_cpu = dkMemBlockGetCpuAddr(staging);
    const DkGpuAddr staging_gpu = dkMemBlockGetGpuAddr(staging);
    if (!staging_cpu || staging_gpu == DK_GPU_ADDR_INVALID) {
        dkMemBlockDestroy(staging);
        return false;
    }
    std::memcpy(staging_cpu, decoded.data(), rgba_bytes);

    dkCmdBufClear(upload_command_buffer);
    dkCmdBufAddMemory(upload_command_buffer, upload_command_mem, 0, UploadCommandMemorySize);
    const DkCopyBuf source_buffer{staging_gpu, entry.key.width, entry.key.height};
    const DkImageRect destination{0, 0, 0, entry.key.width, entry.key.height, 1};
    dkCmdBufCopyBufferToImage(upload_command_buffer, &source_buffer, &entry.view, &destination, 0);
    dkCmdBufBarrier(upload_command_buffer, DkBarrier_Full,
                    DkInvalidateFlags_Image | DkInvalidateFlags_Descriptors);
    const DkCmdList list = dkCmdBufFinishList(upload_command_buffer);
    if (!list) {
        dkMemBlockDestroy(staging);
        return false;
    }
    dkQueueSubmitCommands(queue, list);
    dkQueueSignalFence(queue, &upload_fence, true);
    dkQueueFlush(queue);
    const DkResult wait = dkFenceWait(&upload_fence, 1'000'000'000LL);
    dkMemBlockDestroy(staging);
    if (wait != DkResult_Success || dkQueueIsInErrorState(queue)) {
        return false;
    }

    auto* const image_descriptors = static_cast<DkImageDescriptor*>(descriptor_cpu);
    auto* const sampler_descriptors = reinterpret_cast<DkSamplerDescriptor*>(
        static_cast<u8*>(descriptor_cpu) + descriptor_capacity * sizeof(DkImageDescriptor));
    dkImageDescriptorInitialize(&image_descriptors[entry.image_descriptor], &entry.view, false, false);
    const DkSampler sampler = BuildSampler(texture.config);
    dkSamplerDescriptorInitialize(&sampler_descriptors[entry.sampler_descriptor], &sampler);
    entry.byte_size = TextureByteSize(entry.key);
    RecordTextureUploadBytes(rgba_bytes);
    return true;
}

DkSampler TextureCache::BuildSampler(const Pica::TexturingRegs::TextureConfig& config) {
    DkSampler sampler;
    dkSamplerDefaults(&sampler);
    sampler.minFilter = config.min_filter == Pica::TexturingRegs::TextureConfig::Linear
                            ? DkFilter_Linear
                            : DkFilter_Nearest;
    sampler.magFilter = config.mag_filter == Pica::TexturingRegs::TextureConfig::Linear
                            ? DkFilter_Linear
                            : DkFilter_Nearest;
    sampler.mipFilter = config.mip_filter == Pica::TexturingRegs::TextureConfig::Linear
                            ? DkMipFilter_Linear
                            : DkMipFilter_Nearest;
    sampler.wrapMode[0] = MapWrap(config.wrap_s);
    sampler.wrapMode[1] = MapWrap(config.wrap_t);
    sampler.lodClampMin = static_cast<float>(config.lod.min_level.Value());
    sampler.lodClampMax = static_cast<float>(config.lod.max_level.Value());
    sampler.lodBias = static_cast<float>(config.lod.bias.Value()) / 256.0f;
    sampler.borderColor[0].value_f = config.border_color.r.Value() / 255.0f;
    sampler.borderColor[1].value_f = config.border_color.g.Value() / 255.0f;
    sampler.borderColor[2].value_f = config.border_color.b.Value() / 255.0f;
    sampler.borderColor[3].value_f = config.border_color.a.Value() / 255.0f;
    return sampler;
}

u32 TextureCache::TextureByteSize(const TextureKey& key) {
    const u64 tiles_x = (key.width + 7U) / 8U;
    const u64 tiles_y = (key.height + 7U) / 8U;
    const u64 bytes = tiles_x * tiles_y * Pica::Texture::CalculateTileSize(key.format);
    return static_cast<u32>(std::min<u64>(bytes, std::numeric_limits<u32>::max()));
}

bool TextureCache::Overlaps(PAddr lhs_address, u32 lhs_size, PAddr rhs_address, u32 rhs_size) {
    const u64 lhs_end = static_cast<u64>(lhs_address) + lhs_size;
    const u64 rhs_end = static_cast<u64>(rhs_address) + rhs_size;
    return lhs_address < rhs_end && rhs_address < lhs_end;
}

void TextureCache::DestroyEntry(Entry& entry) {
    if (entry.image_mem) {
        dkMemBlockDestroy(entry.image_mem);
        entry.image_mem = nullptr;
    }
}
#endif

void TextureCache::InvalidateRegion(PAddr address, u32 size) {
#ifdef __SWITCH__
    for (auto& entry : entries) {
        if (!entry->invalidated && Overlaps(address, size, entry->key.address, entry->byte_size)) {
            entry->invalidated = true;
        }
    }
#else
    (void)address;
    (void)size;
#endif
}

void TextureCache::FlushRegion(PAddr address, u32 size) {
    (void)address;
    (void)size;
}

void TextureCache::FlushAll() {}

} // namespace VideoCore::Deko3D
