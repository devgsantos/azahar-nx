// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include "video_core/renderer_software/sw_rasterizer.h"
#include "video_core/rasterizer_interface.h"

namespace Memory {
class MemorySystem;
}

namespace Pica {
class PicaCore;
}

namespace VideoCore::Deko3D {

class Rasterizer final : public RasterizerInterface {
public:
    explicit Rasterizer(Memory::MemorySystem& memory, Pica::PicaCore& pica);

    void AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                     const Pica::OutputVertex& v2) override;
    void DrawTriangles() override;
    void FlushAll() override;
    void FlushRegion(PAddr addr, u32 size) override;
    void InvalidateRegion(PAddr addr, u32 size) override;
    void FlushAndInvalidateRegion(PAddr addr, u32 size) override;
    void ClearAll(bool flush) override;
    bool AccelerateDrawBatch(bool is_indexed) override;

private:
    SwRenderer::RasterizerSoftware software_rasterizer;
};

} // namespace VideoCore::Deko3D
