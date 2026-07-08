// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "common/common_types.h"
#include "video_core/pica/regs_texturing.h"

#ifdef __SWITCH__
#include <deko3d.h>
#endif

namespace Memory {
class MemorySystem;
}

namespace VideoCore::Deko3D {

class State;

class TextureCache {
public:
#ifdef __SWITCH__
    struct BoundTextureSet {
        std::array<DkResHandle, 3> handles{};
        u32 enabled_mask = 0;
        bool valid = true;
    };
#endif

    TextureCache() = default;
    ~TextureCache();

    bool Initialize();
    bool Attach(State& state, Memory::MemorySystem& memory);
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const {
        return initialized;
    }

#ifdef __SWITCH__
    BoundTextureSet PrepareTextures(const Pica::TexturingRegs& regs);
    [[nodiscard]] DkGpuAddr ImageDescriptorGpuAddress() const {
        return image_descriptor_gpu_addr;
    }
    [[nodiscard]] DkGpuAddr SamplerDescriptorGpuAddress() const {
        return sampler_descriptor_gpu_addr;
    }
    [[nodiscard]] u32 DescriptorCapacity() const {
        return descriptor_capacity;
    }
#endif

    void InvalidateRegion(PAddr address, u32 size);
    void FlushRegion(PAddr address, u32 size);
    void FlushAll();

private:
#ifdef __SWITCH__
    struct TextureKey {
        PAddr address = 0;
        u32 width = 0;
        u32 height = 0;
        Pica::TexturingRegs::TextureFormat format{};
        u32 sampler_raw = 0;
        u32 lod_raw = 0;

        bool operator==(const TextureKey& rhs) const {
            return address == rhs.address && width == rhs.width && height == rhs.height &&
                   format == rhs.format && sampler_raw == rhs.sampler_raw && lod_raw == rhs.lod_raw;
        }
    };

    struct Entry {
        TextureKey key{};
        DkMemBlock image_mem{};
        DkImage image{};
        DkImageView view{};
        u32 image_descriptor = 0;
        u32 sampler_descriptor = 0;
        u32 byte_size = 0;
        bool invalidated = false;
    };

    Entry* FindOrCreate(const Pica::TexturingRegs::FullTextureConfig& texture);
    bool UploadEntry(Entry& entry, const Pica::TexturingRegs::FullTextureConfig& texture);
    bool InitializeDescriptorStorage();
    bool InitializeUploadResources();
    void DestroyEntry(Entry& entry);
    static bool Overlaps(PAddr lhs_address, u32 lhs_size, PAddr rhs_address, u32 rhs_size);
    static DkSampler BuildSampler(const Pica::TexturingRegs::TextureConfig& config);
    static u32 TextureByteSize(const TextureKey& key);

    static void RecordTextureCacheHit() {}
    static void RecordTextureCacheMiss() {}
    static void RecordTextureUploadBytes(u64) {}

    State* state = nullptr;
    Memory::MemorySystem* memory = nullptr;
    DkDevice device{};
    DkQueue queue{};

    DkMemBlock descriptor_mem{};
    void* descriptor_cpu = nullptr;
    DkGpuAddr descriptor_gpu = 0;
    DkGpuAddr image_descriptor_gpu_addr = 0;
    DkGpuAddr sampler_descriptor_gpu_addr = 0;
    u32 descriptor_capacity = 1024;
    u32 next_descriptor = 1;

    DkMemBlock upload_command_mem{};
    DkCmdBuf upload_command_buffer{};
    DkFence upload_fence{};

    std::vector<std::unique_ptr<Entry>> entries;
#endif

    bool initialized = false;
};

} // namespace VideoCore::Deko3D
