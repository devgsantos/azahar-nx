// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <cstdint>

namespace VideoCore::Deko3D {

struct PerfStats {
    std::uint64_t hw_draws = 0;
    std::uint64_t hw_triangles = 0;
    std::uint64_t sw_fallback_draws = 0;
    std::uint64_t sw_fallback_triangles = 0;
    std::uint64_t texture_cache_hits = 0;
    std::uint64_t texture_cache_misses = 0;
    std::uint64_t texture_upload_bytes = 0;
    std::uint64_t render_target_cache_hits = 0;
    std::uint64_t render_target_cache_misses = 0;
    std::uint64_t render_target_readbacks = 0;
    std::uint64_t render_target_readback_bytes = 0;
    std::uint64_t unsupported_texture_format = 0;
    std::uint64_t unsupported_tev = 0;
    std::uint64_t unsupported_blend = 0;
    std::uint64_t unsupported_depth = 0;
    std::uint64_t ring_waits = 0;
};

void RecordHardwareDraw(std::uint64_t triangles);
void RecordSoftwareFallback(std::uint64_t triangles);
void RecordRingWait();
PerfStats TakePerfStats();

} // namespace VideoCore::Deko3D
