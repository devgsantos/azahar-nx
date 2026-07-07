// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_stats.h"

#include <atomic>
#include <algorithm>

namespace VideoCore::Deko3D {
namespace {

std::atomic<std::uint64_t> hw_draws{0};
std::atomic<std::uint64_t> hw_triangles{0};
std::atomic<std::uint64_t> hw_draw_attempts{0};
std::atomic<std::uint64_t> hw_draw_successes{0};
std::atomic<std::uint64_t> hw_draw_failures{0};
std::atomic<std::uint64_t> hw_draws_submitted{0};
std::atomic<std::uint64_t> hw_draws_completed{0};
std::atomic<std::uint64_t> hw_triangles_submitted{0};
std::atomic<std::uint64_t> hw_triangles_completed{0};
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
std::atomic<std::uint64_t> fence_poll_successes{0};
std::atomic<std::uint64_t> fence_waits{0};
std::atomic<std::uint64_t> fence_timeouts{0};
std::atomic<std::uint64_t> max_fence_wait_ms{0};
std::atomic<std::uint64_t> queue_errors{0};
std::atomic<std::uint64_t> queue_flushes{0};
std::atomic<std::uint64_t> fallback_textures_enabled{0};
std::atomic<std::uint64_t> fallback_depth_enabled{0};
std::atomic<std::uint64_t> fallback_stencil_enabled{0};
std::atomic<std::uint64_t> fallback_blend_enabled{0};
std::atomic<std::uint64_t> fallback_alpha_test{0};
std::atomic<std::uint64_t> fallback_logic_op{0};
std::atomic<std::uint64_t> fallback_geometry_shader{0};
std::atomic<std::uint64_t> fallback_wrong_render_target{0};
std::atomic<std::uint64_t> fallback_framebuffer_format{0};
std::atomic<std::uint64_t> fallback_topology{0};
std::atomic<std::uint64_t> fallback_shadow{0};
std::atomic<std::uint64_t> fallback_unsupported_state{0};

std::uint64_t Take(std::atomic<std::uint64_t>& value) {
    return value.exchange(0, std::memory_order_relaxed);
}

} // namespace

void RecordHardwareDrawSubmitted(std::uint64_t triangles) {
    hw_draws_submitted.fetch_add(1, std::memory_order_relaxed);
    hw_triangles_submitted.fetch_add(triangles, std::memory_order_relaxed);
}

void RecordHardwareDrawCompleted(std::uint64_t triangles) {
    hw_draws.fetch_add(1, std::memory_order_relaxed);
    hw_draws_completed.fetch_add(1, std::memory_order_relaxed);
    hw_draw_successes.fetch_add(1, std::memory_order_relaxed);
    hw_triangles.fetch_add(triangles, std::memory_order_relaxed);
    hw_triangles_completed.fetch_add(triangles, std::memory_order_relaxed);
}

void RecordHardwareDrawAttempt() {
    hw_draw_attempts.fetch_add(1, std::memory_order_relaxed);
}

void RecordHardwareDrawFailure() {
    hw_draw_failures.fetch_add(1, std::memory_order_relaxed);
}

void RecordSoftwareFallback(std::uint64_t triangles) {
    sw_fallback_draws.fetch_add(1, std::memory_order_relaxed);
    sw_fallback_triangles.fetch_add(triangles, std::memory_order_relaxed);
}

void RecordRingWait() {
    ring_waits.fetch_add(1, std::memory_order_relaxed);
}

void RecordFencePollSuccess() {
    fence_poll_successes.fetch_add(1, std::memory_order_relaxed);
}

void RecordFenceWait() {
    fence_waits.fetch_add(1, std::memory_order_relaxed);
}

void RecordFenceWaitDurationMs(std::uint64_t duration_ms) {
    std::uint64_t current = max_fence_wait_ms.load(std::memory_order_relaxed);
    while (duration_ms > current &&
           !max_fence_wait_ms.compare_exchange_weak(current, duration_ms,
                                                    std::memory_order_relaxed)) {
    }
}

void RecordFenceTimeout() {
    fence_timeouts.fetch_add(1, std::memory_order_relaxed);
}

void RecordQueueError() {
    queue_errors.fetch_add(1, std::memory_order_relaxed);
}

void RecordQueueFlush() {
    queue_flushes.fetch_add(1, std::memory_order_relaxed);
}

void RecordFallbackReason(FallbackReason reason) {
    switch (reason) {
    case FallbackReason::TexturesEnabled:
        fallback_textures_enabled.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::DepthEnabled:
        fallback_depth_enabled.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::StencilEnabled:
        fallback_stencil_enabled.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::BlendEnabled:
        fallback_blend_enabled.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::AlphaTest:
        fallback_alpha_test.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::LogicOp:
        fallback_logic_op.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::GeometryShader:
        fallback_geometry_shader.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::WrongRenderTarget:
        fallback_wrong_render_target.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::FramebufferFormat:
        fallback_framebuffer_format.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::Topology:
        fallback_topology.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::Shadow:
        fallback_shadow.fetch_add(1, std::memory_order_relaxed);
        break;
    case FallbackReason::UnsupportedState:
        fallback_unsupported_state.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

PerfStats TakePerfStats() {
    PerfStats stats{};
    stats.hw_draws = Take(hw_draws);
    stats.hw_triangles = Take(hw_triangles);
    stats.hw_draw_attempts = Take(hw_draw_attempts);
    stats.hw_draw_successes = Take(hw_draw_successes);
    stats.hw_draw_failures = Take(hw_draw_failures);
    stats.hw_draws_submitted = Take(hw_draws_submitted);
    stats.hw_draws_completed = Take(hw_draws_completed);
    stats.hw_triangles_submitted = Take(hw_triangles_submitted);
    stats.hw_triangles_completed = Take(hw_triangles_completed);
    stats.sw_fallback_draws = Take(sw_fallback_draws);
    stats.sw_fallback_triangles = Take(sw_fallback_triangles);
    const std::uint64_t total_triangles = stats.hw_triangles_completed + stats.sw_fallback_triangles;
    if (total_triangles != 0) {
        stats.hw_coverage_percent =
            (static_cast<double>(stats.hw_triangles) * 100.0) /
            static_cast<double>(total_triangles);
    }
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
    stats.fence_poll_successes = Take(fence_poll_successes);
    stats.fence_waits = Take(fence_waits);
    stats.fence_timeouts = Take(fence_timeouts);
    stats.max_fence_wait_ms = Take(max_fence_wait_ms);
    stats.queue_errors = Take(queue_errors);
    stats.queue_flushes = Take(queue_flushes);
    stats.fallback_textures_enabled = Take(fallback_textures_enabled);
    stats.fallback_depth_enabled = Take(fallback_depth_enabled);
    stats.fallback_stencil_enabled = Take(fallback_stencil_enabled);
    stats.fallback_blend_enabled = Take(fallback_blend_enabled);
    stats.fallback_alpha_test = Take(fallback_alpha_test);
    stats.fallback_logic_op = Take(fallback_logic_op);
    stats.fallback_geometry_shader = Take(fallback_geometry_shader);
    stats.fallback_wrong_render_target = Take(fallback_wrong_render_target);
    stats.fallback_framebuffer_format = Take(fallback_framebuffer_format);
    stats.fallback_topology = Take(fallback_topology);
    stats.fallback_shadow = Take(fallback_shadow);
    stats.fallback_unsupported_state = Take(fallback_unsupported_state);
    return stats;
}

} // namespace VideoCore::Deko3D
