// Native-only Deko3D rasterizer class overlay.
#pragma once

#if defined(__SWITCH__) && defined(AZAHAR_DEKO3D_NATIVE_RUNTIME)

#include "video_core/renderer_software/sw_rasterizer.h"

namespace VideoCore::Deko3D::NativeSwRenderer {

// Preserve the existing Rasterizer class layout and call sites while making its compatibility
// object a zero-cost discard sink. Unsupported native state is allowed to render with reduced
// fidelity or is skipped; it is never rasterized on the CPU in the native runtime build.
class RasterizerSoftware final : public VideoCore::RasterizerInterface {
public:
    explicit RasterizerSoftware(Memory::MemorySystem&, Pica::PicaCore&) {}

    void AddTriangle(const Pica::OutputVertex&, const Pica::OutputVertex&,
                     const Pica::OutputVertex&) override {}
    void DrawTriangles() override {}
    void FlushAll() override {}
    void FlushRegion(PAddr, u32) override {}
    void InvalidateRegion(PAddr, u32) override {}
    void FlushAndInvalidateRegion(PAddr, u32) override {}
    void ClearAll(bool) override {}
};

} // namespace VideoCore::Deko3D::NativeSwRenderer

#define SwRenderer NativeSwRenderer
#include "video_core/renderer_deko3d/deko3d_rasterizer.h"
#undef SwRenderer

#else
#include "video_core/renderer_deko3d/deko3d_rasterizer.h"
#endif
