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
    bool AllocateTexture(CachedTexture& cached, u32 width, u32 height,
                          Pica::TexturingRegs::TextureFormat format);
    bool UploadTexture(CachedTexture& cached, const Pica::TexturingRegs::TextureConfig& config,
                       Pica::TexturingRegs::TextureFormat format);
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
    DkMemBlock upload_command_mem_block{};
    DkCmdBuf upload_command_buffer{};
    std::unordered_map<u64, std::unique_ptr<CachedTexture>> cache;
#endif
    bool initialized = false;
};

} // namespace VideoCore::Deko3D
