// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_rasterizer.h"

#include "common/logging/log.h"

namespace VideoCore::Deko3D {
namespace {

void LogSoftwareBridgeOnce() {
    static bool logged = false;
    if (!logged) {
        LOG_INFO(Render,
                 "Deko3D rasterizer: using software PICA rasterization with native Deko3D "
                 "presentation");
        logged = true;
    }
}

} // namespace

Rasterizer::Rasterizer(Memory::MemorySystem& memory, Pica::PicaCore& pica)
    : software_rasterizer{memory, pica} {}

void Rasterizer::AddTriangle(const Pica::OutputVertex& v0, const Pica::OutputVertex& v1,
                             const Pica::OutputVertex& v2) {
    LogSoftwareBridgeOnce();
    software_rasterizer.AddTriangle(v0, v1, v2);
}

void Rasterizer::DrawTriangles() {
    software_rasterizer.DrawTriangles();
}

void Rasterizer::FlushAll() {
    software_rasterizer.FlushAll();
}

void Rasterizer::FlushRegion(PAddr addr, u32 size) {
    software_rasterizer.FlushRegion(addr, size);
}

void Rasterizer::InvalidateRegion(PAddr addr, u32 size) {
    software_rasterizer.InvalidateRegion(addr, size);
}

void Rasterizer::FlushAndInvalidateRegion(PAddr addr, u32 size) {
    software_rasterizer.FlushAndInvalidateRegion(addr, size);
}

void Rasterizer::ClearAll(bool flush) {
    software_rasterizer.ClearAll(flush);
}

bool Rasterizer::AccelerateDrawBatch(bool is_indexed) {
    return software_rasterizer.AccelerateDrawBatch(is_indexed);
}

} // namespace VideoCore::Deko3D
