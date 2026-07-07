// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <cstdint>

namespace VideoCore::Deko3D {

struct PerfStats {
    std::uint64_t hw_draws = 0;
    std::uint64_t hw_triangles = 0;
    std::uint64_t hw_draw_attempts = 0;
    std::uint64_t hw_draw_successes = 0;
    std::uint64_t hw_draw_failures = 0;
    std::uint64_t hw_draws_submitted = 0;
    std::uint64_t hw_draws_completed = 0;
    std::uint64_t hw_triangles_submitted = 0;
    std::uint64_t hw_triangles_completed = 0;
    double hw_coverage_percent = 0.0;
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
    std::uint64_t fence_poll_successes = 0;
    std::uint64_t fence_waits = 0;
    std::uint64_t fence_timeouts = 0;
    std::uint64_t max_fence_wait_ms = 0;
    std::uint64_t queue_errors = 0;
    std::uint64_t queue_flushes = 0;
    std::uint64_t fallback_textures_enabled = 0;
    std::uint64_t fallback_depth_enabled = 0;
    std::uint64_t fallback_stencil_enabled = 0;
    std::uint64_t fallback_blend_enabled = 0;
    std::uint64_t fallback_alpha_test = 0;
    std::uint64_t fallback_logic_op = 0;
    std::uint64_t fallback_geometry_shader = 0;
    std::uint64_t fallback_wrong_render_target = 0;
    std::uint64_t fallback_framebuffer_format = 0;
    std::uint64_t fallback_topology = 0;
    std::uint64_t fallback_shadow = 0;
    std::uint64_t fallback_unsupported_state = 0;
};

enum class FallbackReason {
    TexturesEnabled,
    DepthEnabled,
    StencilEnabled,
    BlendEnabled,
    AlphaTest,
    LogicOp,
    GeometryShader,
    WrongRenderTarget,
    FramebufferFormat,
    Topology,
    Shadow,
    UnsupportedState,
};

void RecordHardwareDrawSubmitted(std::uint64_t triangles);
void RecordHardwareDrawCompleted(std::uint64_t triangles);
void RecordHardwareDrawAttempt();
void RecordHardwareDrawFailure();
void RecordSoftwareFallback(std::uint64_t triangles);
void RecordRingWait();
void RecordFencePollSuccess();
void RecordFenceWait();
void RecordFenceWaitDurationMs(std::uint64_t duration_ms);
void RecordFenceTimeout();
void RecordQueueError();
void RecordQueueFlush();
void RecordFallbackReason(FallbackReason reason);
PerfStats TakePerfStats();

} // namespace VideoCore::Deko3D
