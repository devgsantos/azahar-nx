// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <memory>
#include <optional>
#include <unordered_map>

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

#ifdef __SWITCH__
struct CachedTexture {
    PAddr physical_address{};
    u32 source_bytes = 0;
    u32 width = 0;
    u32 height = 0;
    Pica::TexturingRegs::TextureFormat format{};
    DkImage image{};
    DkImageView view{};
    DkMemBlock mem_block{};
    DkSampler sampler{};
    u64 allocation_bytes = 0;
    u64 last_used_generation = 0;
    u32 upload_slot = 0xFFFFFFFFu;
    u64 upload_serial = 0;
};
#endif

class TextureCache {
public:
    ~TextureCache();

    bool Initialize(State& state, Memory::MemorySystem& memory);
    void Shutdown();
    bool IsInitialized() const {
        return initialized;
    }

#ifdef __SWITCH__
    /// Look up or create a GPU texture for the given PICA texture unit config.
    /// Returns nullptr if the texture cannot be accelerated.
    [[nodiscard]] const CachedTexture* GetTexture(
        const Pica::TexturingRegs::FullTextureConfig& config);

    /// Mark any cached textures overlapping the given guest memory range as stale.
    void InvalidateRegion(PAddr address, u32 size);
    void FlushRegion(PAddr address, u32 size);
    void FlushAndInvalidateRegion(PAddr address, u32 size);
#endif

private:
#ifdef __SWITCH__
    static constexpr u32 UploadSlotCount = 2;
    static constexpr u32 UploadStagingSliceSize = 4 * 1024 * 1024;
    static constexpr u32 UploadCommandSize = 16 * 1024;
    static constexpr u64 TextureCacheBudgetBytes = 96ULL * 1024 * 1024;

    struct UploadSlot {
        DkMemBlock command_mem_block{};
        DkCmdBuf command_buffer{};
        DkFence fence{};
        u32 staging_offset = 0;
        u64 serial = 0;
        bool fence_pending = false;
    };

    bool AllocateTexture(CachedTexture& cached, u32 width, u32 height,
                         Pica::TexturingRegs::TextureFormat format);
    bool UploadTexture(CachedTexture& cached, const Pica::TexturingRegs::TextureConfig& config,
                       Pica::TexturingRegs::TextureFormat format);
    bool WaitForUploadSlot(UploadSlot& slot);
    bool WaitForTextureUpload(CachedTexture& cached);
    bool EvictForAllocation(u64 required_bytes);
    void DestroyTexture(CachedTexture& cached);
    DkSampler CreateSampler(const Pica::TexturingRegs::TextureConfig& config) const;
    std::optional<DkImageFormat> MapTextureFormat(Pica::TexturingRegs::TextureFormat format) const;
    u64 ComputeTextureKey(const Pica::TexturingRegs::FullTextureConfig& config) const;

    State* state = nullptr;
    Memory::MemorySystem* memory = nullptr;
    DkDevice device{};
    DkMemBlock staging_mem_block{};
    DkGpuAddr staging_gpu_addr = 0;
    void* staging_cpu_addr = nullptr;
    std::array<UploadSlot, UploadSlotCount> upload_slots{};
    u32 current_upload_slot = 0;
    u64 next_upload_serial = 0;
    u64 cache_generation = 0;
    u64 cache_allocation_bytes = 0;
    std::unordered_map<u64, std::unique_ptr<CachedTexture>> cache;
#endif
    bool initialized = false;
};

} // namespace VideoCore::Deko3D
