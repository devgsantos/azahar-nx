// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_stats.h"

#include <atomic>

namespace VideoCore::Deko3D {
namespace {

std::atomic<std::uint64_t> hw_draws{0};
std::atomic<std::uint64_t> hw_triangles{0};
std::atomic<std::uint64_t> sw_fallback_draws{0};
std::atomic<std::uint64_t> sw_fallback_triangles{0};
std::atomic<std::uint64_t> texture_cache_hits{0};
std::atomic<std::uint64_t> texture_cache_misses{0};
std::atomic<std::uint64_t> texture_upload_bytes{0};
std::atomic<std::uint64_t> render_target_cache_hits{0};
std::atomic<std::uint64_t> render_target_cache_misses{0};
std::atomic<std::uint64_t> render_target_readbacks{0};
std::atomic<std::uint64_t> render_target_readback_bytes{0};
std::atomic<std::uint64_t> unsupported_texture_format{0};
std::atomic<std::uint64_t> unsupported_tev{0};
std::atomic<std::uint64_t> unsupported_blend{0};
std::atomic<std::uint64_t> unsupported_depth{0};
std::atomic<std::uint64_t> ring_waits{0};

std::uint64_t Take(std::atomic<std::uint64_t>& value) {
    return value.exchange(0, std::memory_order_relaxed);
}

} // namespace

void RecordHardwareDraw(std::uint64_t triangles) {
    hw_draws.fetch_add(1, std::memory_order_relaxed);
    hw_triangles.fetch_add(triangles, std::memory_order_relaxed);
}

void RecordSoftwareFallback(std::uint64_t triangles) {
    sw_fallback_draws.fetch_add(1, std::memory_order_relaxed);
    sw_fallback_triangles.fetch_add(triangles, std::memory_order_relaxed);
}

void RecordRingWait() {
    ring_waits.fetch_add(1, std::memory_order_relaxed);
}

PerfStats TakePerfStats() {
    PerfStats stats{};
    stats.hw_draws = Take(hw_draws);
    stats.hw_triangles = Take(hw_triangles);
    stats.sw_fallback_draws = Take(sw_fallback_draws);
    stats.sw_fallback_triangles = Take(sw_fallback_triangles);
    stats.texture_cache_hits = Take(texture_cache_hits);
    stats.texture_cache_misses = Take(texture_cache_misses);
    stats.texture_upload_bytes = Take(texture_upload_bytes);
    stats.render_target_cache_hits = Take(render_target_cache_hits);
    stats.render_target_cache_misses = Take(render_target_cache_misses);
    stats.render_target_readbacks = Take(render_target_readbacks);
    stats.render_target_readback_bytes = Take(render_target_readback_bytes);
    stats.unsupported_texture_format = Take(unsupported_texture_format);
    stats.unsupported_tev = Take(unsupported_tev);
    stats.unsupported_blend = Take(unsupported_blend);
    stats.unsupported_depth = Take(unsupported_depth);
    stats.ring_waits = Take(ring_waits);
    return stats;
}

} // namespace VideoCore::Deko3D
