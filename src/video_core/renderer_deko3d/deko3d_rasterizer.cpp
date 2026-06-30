// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_rasterizer.h"

#include "common/logging/log.h"

namespace VideoCore::Deko3D {
namespace {

void LogOnce(const char* message) {
    static bool logged = false;
    if (!logged) {
        LOG_WARNING(Render, "Deko3D rasterizer: {}", message);
        logged = true;
    }
}

} // namespace

void Rasterizer::AddTriangle(const Pica::OutputVertex&, const Pica::OutputVertex&,
                             const Pica::OutputVertex&) {
    LogOnce("PICA triangle path is not fully translated yet");
}

void Rasterizer::DrawTriangles() {
    LogOnce("DrawTriangles reached before full Deko3D PICA pipeline translation");
}

void Rasterizer::FlushAll() {}

void Rasterizer::FlushRegion(PAddr, u32) {}

void Rasterizer::InvalidateRegion(PAddr, u32) {}

void Rasterizer::FlushAndInvalidateRegion(PAddr, u32) {}

void Rasterizer::ClearAll(bool) {}

bool Rasterizer::AccelerateDrawBatch(bool) {
    LogOnce("AccelerateDrawBatch is not complete");
    return false;
}

} // namespace VideoCore::Deko3D
