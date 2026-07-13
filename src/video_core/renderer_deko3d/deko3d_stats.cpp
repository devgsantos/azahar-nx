// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_stats.h"

#include <algorithm>
#include <atomic>

namespace VideoCore::Deko3D {
namespace {

#define DEKO_EXTRA_COUNTERS(X)                                                                    \
    X(raster_queue_submits)                                                                        \
    X(raster_queue_flushes)                                                                        \
    X(raster_fence_polls)                                                                          \
    X(raster_fence_poll_successes)                                                                 \
    X(raster_fence_waits)                                                                          \
    X(raster_fence_timeouts)                                                                       \
    X(raster_max_fence_wait_us)                                                                    \
    X(raster_queue_errors)                                                                         \
    X(present_queue_submits)                                                                       \
    X(present_queue_flushes)                                                                       \
    X(present_fence_polls)                                                                         \
    X(present_fence_poll_successes)                                                                \
    X(present_fence_waits)                                                                         \
    X(present_fence_timeouts)                                                                      \
    X(present_max_fence_wait_us)                                                                   \
    X(present_queue_errors)                                                                        \
    X(fallback_invalid_transformed_batch)                                                          \
    X(transformed_batch_checks)                                                                    \
    X(transformed_batch_valid)                                                                     \
    X(transformed_batch_invalid)                                                                   \
    X(transformed_batch_eligible)                                                                  \
    X(transformed_batch_submitted)                                                                 \
    X(transformed_batch_completed)                                                                 \
    X(transformed_vertices_submitted)                                                              \
    X(transformed_vertices_completed)                                                              \
    X(direct_batch_checks)                                                                         \
    X(direct_batch_rejected)                                                                       \
    X(transformed_blocker_invalid_batch)                                                           \
    X(transformed_blocker_missing_gpu_resources)                                                    \
    X(transformed_blocker_shader_unavailable)                                                       \
    X(transformed_blocker_wrong_render_target)                                                      \
    X(transformed_blocker_framebuffer_format)                                                       \
    X(transformed_blocker_framebuffer_dimensions)                                                   \
    X(transformed_blocker_textures_enabled)                                                         \
    X(transformed_blocker_depth_test_enabled)                                                       \
    X(transformed_blocker_depth_write_enabled)                                                      \
    X(transformed_blocker_stencil_enabled)                                                          \
    X(transformed_blocker_blending_enabled)                                                         \
    X(transformed_blocker_alpha_test)                                                               \
    X(transformed_blocker_logic_op)                                                                 \
    X(transformed_blocker_color_mask)                                                               \
    X(direct_blocker_unimplemented)                                                                \
    X(direct_blocker_topology)                                                                      \
    X(direct_blocker_geometry_shader)                                                               \
    X(direct_blocker_vertex_format)                                                                \
    X(direct_blocker_index_format)                                                                 \
    X(direct_blocker_other)                                                                        \
    X(blocker_invalid_batch)                                                                       \
    X(blocker_missing_gpu_resources)                                                               \
    X(blocker_shader_unavailable)                                                                  \
    X(blocker_wrong_render_target)                                                                 \
    X(blocker_framebuffer_format)                                                                  \
    X(blocker_framebuffer_dimensions)                                                              \
    X(blocker_textures_enabled)                                                                    \
    X(blocker_depth_test_enabled)                                                                  \
    X(blocker_depth_write_enabled)                                                                 \
    X(blocker_stencil_enabled)                                                                     \
    X(blocker_blending_enabled)                                                                    \
    X(blocker_alpha_test)                                                                          \
    X(blocker_logic_op)                                                                            \
    X(blocker_color_mask)                                                                          \
    X(blocker_cull_mode)                                                                           \
    X(blocker_viewport)                                                                            \
    X(blocker_scissor)                                                                             \
    X(blocker_shadow)                                                                              \
    X(blocker_procedural_texture)                                                                  \
    X(batches_with_multiple_blockers)                                                              \
    X(partial_batches)                                                                             \
    X(partial_hw_triangles)                                                                        \
    X(partial_sw_triangles)                                                                        \
    X(duplicate_triangle_preventions)                                                              \
    X(dropped_triangle_detections)                                                                 \
    X(pica_command_lists_processed)                                                                \
    X(pica_commands_processed)                                                                     \
    X(pica_draw_array_commands)                                                                    \
    X(pica_draw_indexed_commands)                                                                  \
    X(pica_output_triangles)                                                                       \
    X(pica_output_vertices)                                                                        \
    X(pica_memory_fill_requests)                                                                   \
    X(pica_memory_fill_completed)                                                                  \
    X(pica_memory_fill_bytes)                                                                      \
    X(pica_display_transfer_requests)                                                              \
    X(pica_display_transfer_completed)                                                             \
    X(pica_display_transfer_bytes)                                                                 \
    X(pica_texture_copy_requests)                                                                  \
    X(pica_texture_copy_completed)                                                                 \
    X(pica_texture_copy_bytes)                                                                     \
    X(pica_cache_flush_requests)                                                                   \
    X(pica_cache_invalidation_requests)                                                            \
    X(gsp_interrupts_requested)                                                                    \
    X(gsp_interrupts_delivered)                                                                    \
    X(gsp_interrupts_dropped)                                                                      \
    X(gsp_logical_interrupts_raised)                                                               \
    X(gsp_thread_delivery_attempts)                                                                \
    X(gsp_interrupts_ignored_no_active_thread)                                                      \
    X(gsp_interrupts_ignored_unregistered_thread)                                                   \
    X(gsp_interrupts_ignored_no_event)                                                             \
    X(gsp_interrupts_queue_full)                                                                   \
    X(gsp_interrupts_stale_scheduled_event)                                                        \
    X(gsp_interrupts_actual_dropped)                                                               \
    X(top_framebuffer_address_changes)                                                             \
    X(bottom_framebuffer_address_changes)                                                          \
    X(framebuffer_format_changes)                                                                  \
    X(framebuffer_dimension_changes)                                                               \
    X(framebuffer_stride_changes)                                                                  \
    X(present_calls)                                                                               \
    X(present_changed_frames)                                                                      \
    X(present_unchanged_frames)                                                                    \
    X(present_top_screen_updates)                                                                  \
    X(present_bottom_screen_updates)                                                               \
    X(present_source_unknown)                                                                      \
    X(present_source_cpu_upload)                                                                   \
    X(present_source_software_rasterizer)                                                          \
    X(present_source_deko_render_target)                                                           \
    X(present_source_display_transfer)                                                             \
    X(present_source_memory_fill)                                                                  \
    X(present_source_framebuffer_change)                                                           \
    X(present_source_cached_render_target)                                                         \
    X(present_source_cached_render_target_blit)                                                     \
    X(present_source_cached_render_target_readback)                                                 \
    X(present_source_repeated_frame)                                                               \
    X(present_source_clear_fallback)                                                               \
    X(emulated_system_frames)                                                                      \
    X(emulated_vblanks)                                                                            \
    X(game_frame_counter)                                                                          \
    X(changed_presented_frames)                                                                    \
    X(repeated_presented_frames)                                                                   \
    X(hardware_raster_frames)                                                                      \
    X(software_raster_frames)                                                                      \
    X(transfer_only_frames)                                                                        \
    X(deko_blend_state_supported)                                                                  \
    X(deko_blend_state_unsupported)                                                                \
    X(deko_blend_pipeline_cache_hits)                                                              \
    X(deko_blend_pipeline_cache_misses)                                                            \
    X(deko_depth_state_supported)                                                                  \
    X(deko_depth_state_unsupported)                                                                \
    X(deko_state_signature_count)                                                                  \
    X(deko_state_signature_id)                                                                     \
    X(render_target_cpu_dirty_by_cpu_memory)                                                        \
    X(render_target_cpu_dirty_by_software)                                                         \
    X(render_target_cpu_dirty_by_display_transfer)

#define ATOMIC_COUNTER(name) std::atomic<std::uint64_t> name{0};

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
std::atomic<std::uint64_t> render_target_cache_creations{0};
std::atomic<std::uint64_t> render_target_cache_evictions{0};
std::atomic<std::uint64_t> render_target_cache_bytes{0};
std::atomic<std::uint64_t> render_target_gpu_dirty{0};
std::atomic<std::uint64_t> render_target_cpu_dirty{0};
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
DEKO_EXTRA_COUNTERS(ATOMIC_COUNTER)

std::uint64_t Take(std::atomic<std::uint64_t>& value) {
    return value.exchange(0, std::memory_order_relaxed);
}

void AddStats(PerfStats& total, const PerfStats& interval) {
    total.hw_draws += interval.hw_draws;
    total.hw_triangles += interval.hw_triangles;
    total.hw_draw_attempts += interval.hw_draw_attempts;
    total.hw_draw_successes += interval.hw_draw_successes;
    total.hw_draw_failures += interval.hw_draw_failures;
    total.hw_draws_submitted += interval.hw_draws_submitted;
    total.hw_draws_completed += interval.hw_draws_completed;
    total.hw_triangles_submitted += interval.hw_triangles_submitted;
    total.hw_triangles_completed += interval.hw_triangles_completed;
    total.sw_fallback_draws += interval.sw_fallback_draws;
    total.sw_fallback_triangles += interval.sw_fallback_triangles;
    total.texture_cache_hits += interval.texture_cache_hits;
    total.texture_cache_misses += interval.texture_cache_misses;
    total.texture_upload_bytes += interval.texture_upload_bytes;
    total.render_target_cache_hits += interval.render_target_cache_hits;
    total.render_target_cache_misses += interval.render_target_cache_misses;
    total.render_target_cache_creations += interval.render_target_cache_creations;
    total.render_target_cache_evictions += interval.render_target_cache_evictions;
    total.render_target_cache_bytes = interval.render_target_cache_bytes;
    total.render_target_gpu_dirty += interval.render_target_gpu_dirty;
    total.render_target_cpu_dirty += interval.render_target_cpu_dirty;
    total.render_target_readbacks += interval.render_target_readbacks;
    total.render_target_readback_bytes += interval.render_target_readback_bytes;
    total.unsupported_texture_format += interval.unsupported_texture_format;
    total.unsupported_tev += interval.unsupported_tev;
    total.unsupported_blend += interval.unsupported_blend;
    total.unsupported_depth += interval.unsupported_depth;
    total.ring_waits += interval.ring_waits;
    total.fence_poll_successes += interval.fence_poll_successes;
    total.fence_waits += interval.fence_waits;
    total.fence_timeouts += interval.fence_timeouts;
    total.max_fence_wait_ms = std::max(total.max_fence_wait_ms, interval.max_fence_wait_ms);
    total.queue_errors += interval.queue_errors;
    total.queue_flushes += interval.queue_flushes;
    total.fallback_textures_enabled += interval.fallback_textures_enabled;
    total.fallback_depth_enabled += interval.fallback_depth_enabled;
    total.fallback_stencil_enabled += interval.fallback_stencil_enabled;
    total.fallback_blend_enabled += interval.fallback_blend_enabled;
    total.fallback_alpha_test += interval.fallback_alpha_test;
    total.fallback_logic_op += interval.fallback_logic_op;
    total.fallback_geometry_shader += interval.fallback_geometry_shader;
    total.fallback_wrong_render_target += interval.fallback_wrong_render_target;
    total.fallback_framebuffer_format += interval.fallback_framebuffer_format;
    total.fallback_topology += interval.fallback_topology;
    total.fallback_shadow += interval.fallback_shadow;
    total.fallback_unsupported_state += interval.fallback_unsupported_state;
    const auto previous_raster_max = total.raster_max_fence_wait_us;
    const auto previous_present_max = total.present_max_fence_wait_us;
#define ADD_EXTRA_COUNTER(name) total.name += interval.name;
    DEKO_EXTRA_COUNTERS(ADD_EXTRA_COUNTER)
#undef ADD_EXTRA_COUNTER
    total.raster_max_fence_wait_us =
        std::max(previous_raster_max, interval.raster_max_fence_wait_us);
    total.present_max_fence_wait_us =
        std::max(previous_present_max, interval.present_max_fence_wait_us);
    total.deko_state_signature_id = interval.deko_state_signature_id != 0
                                        ? interval.deko_state_signature_id
                                        : total.deko_state_signature_id;
    const std::uint64_t total_triangles =
        total.hw_triangles_completed + total.sw_fallback_triangles;
    total.hw_coverage_percent =
        total_triangles == 0
            ? 0.0
            : (static_cast<double>(total.hw_triangles) * 100.0) /
                  static_cast<double>(total_triangles);
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

void RecordRasterQueueSubmit() {
    raster_queue_submits.fetch_add(1, std::memory_order_relaxed);
}

void RecordRasterQueueFlush() {
    raster_queue_flushes.fetch_add(1, std::memory_order_relaxed);
}

void RecordRasterFencePoll() {
    raster_fence_polls.fetch_add(1, std::memory_order_relaxed);
}

void RecordRasterFencePollSuccess() {
    raster_fence_poll_successes.fetch_add(1, std::memory_order_relaxed);
}

void RecordRasterFenceWait() {
    raster_fence_waits.fetch_add(1, std::memory_order_relaxed);
}

void RecordRasterFenceTimeout() {
    raster_fence_timeouts.fetch_add(1, std::memory_order_relaxed);
}

void RecordRasterFenceWaitDurationUs(std::uint64_t duration_us) {
    std::uint64_t current = raster_max_fence_wait_us.load(std::memory_order_relaxed);
    while (duration_us > current &&
           !raster_max_fence_wait_us.compare_exchange_weak(current, duration_us,
                                                           std::memory_order_relaxed)) {
    }
}

void RecordRasterQueueError() {
    raster_queue_errors.fetch_add(1, std::memory_order_relaxed);
}

void RecordPresentQueueSubmit() {
    present_queue_submits.fetch_add(1, std::memory_order_relaxed);
}

void RecordPresentQueueFlush() {
    present_queue_flushes.fetch_add(1, std::memory_order_relaxed);
}

void RecordPresentFencePoll() {
    present_fence_polls.fetch_add(1, std::memory_order_relaxed);
}

void RecordPresentFencePollSuccess() {
    present_fence_poll_successes.fetch_add(1, std::memory_order_relaxed);
}

void RecordPresentFenceWait() {
    present_fence_waits.fetch_add(1, std::memory_order_relaxed);
}

void RecordPresentFenceTimeout() {
    present_fence_timeouts.fetch_add(1, std::memory_order_relaxed);
}

void RecordPresentFenceWaitDurationUs(std::uint64_t duration_us) {
    std::uint64_t current = present_max_fence_wait_us.load(std::memory_order_relaxed);
    while (duration_us > current &&
           !present_max_fence_wait_us.compare_exchange_weak(current, duration_us,
                                                            std::memory_order_relaxed)) {
    }
}

void RecordPresentQueueError() {
    present_queue_errors.fetch_add(1, std::memory_order_relaxed);
}

void RecordTransformedBatchCheck(bool valid) {
    transformed_batch_checks.fetch_add(1, std::memory_order_relaxed);
    if (valid) {
        transformed_batch_valid.fetch_add(1, std::memory_order_relaxed);
    } else {
        transformed_batch_invalid.fetch_add(1, std::memory_order_relaxed);
    }
}

void RecordTransformedBatchEligible() {
    transformed_batch_eligible.fetch_add(1, std::memory_order_relaxed);
}

void RecordTransformedBatchSubmitted(std::uint64_t vertices) {
    transformed_batch_submitted.fetch_add(1, std::memory_order_relaxed);
    transformed_vertices_submitted.fetch_add(vertices, std::memory_order_relaxed);
}

void RecordTransformedBatchCompleted(std::uint64_t vertices) {
    transformed_batch_completed.fetch_add(1, std::memory_order_relaxed);
    transformed_vertices_completed.fetch_add(vertices, std::memory_order_relaxed);
}

void RecordDirectBatchRejected() {
    direct_batch_checks.fetch_add(1, std::memory_order_relaxed);
    direct_batch_rejected.fetch_add(1, std::memory_order_relaxed);
}

void RecordFallbackInvalidTransformedBatch() {
    fallback_invalid_transformed_batch.fetch_add(1, std::memory_order_relaxed);
}

void RecordBlocker(std::uint32_t blocker_mask) {
    RecordTransformedBlocker(blocker_mask);
}

void RecordTransformedBlocker(std::uint32_t blocker_mask) {
    constexpr std::uint32_t invalid_batch = 1U << 0;
    constexpr std::uint32_t missing_gpu = 1U << 1;
    constexpr std::uint32_t shader = 1U << 2;
    constexpr std::uint32_t wrong_rt = 1U << 3;
    constexpr std::uint32_t format = 1U << 4;
    constexpr std::uint32_t dimensions = 1U << 5;
    constexpr std::uint32_t textures = 1U << 6;
    constexpr std::uint32_t depth_test = 1U << 7;
    constexpr std::uint32_t depth_write = 1U << 8;
    constexpr std::uint32_t stencil = 1U << 9;
    constexpr std::uint32_t blend = 1U << 10;
    constexpr std::uint32_t alpha = 1U << 11;
    constexpr std::uint32_t logic = 1U << 12;
    constexpr std::uint32_t color_mask = 1U << 13;
    constexpr std::uint32_t cull = 1U << 14;
    constexpr std::uint32_t viewport = 1U << 15;
    constexpr std::uint32_t scissor = 1U << 16;
    constexpr std::uint32_t shadow = 1U << 17;
    constexpr std::uint32_t proctex = 1U << 18;

    auto add_if = [blocker_mask](std::uint32_t bit, std::atomic<std::uint64_t>& counter) {
        if ((blocker_mask & bit) != 0) {
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    };
    add_if(invalid_batch, blocker_invalid_batch);
    add_if(invalid_batch, transformed_blocker_invalid_batch);
    add_if(missing_gpu, blocker_missing_gpu_resources);
    add_if(missing_gpu, transformed_blocker_missing_gpu_resources);
    add_if(shader, blocker_shader_unavailable);
    add_if(shader, transformed_blocker_shader_unavailable);
    add_if(wrong_rt, blocker_wrong_render_target);
    add_if(wrong_rt, transformed_blocker_wrong_render_target);
    add_if(format, blocker_framebuffer_format);
    add_if(format, transformed_blocker_framebuffer_format);
    add_if(dimensions, blocker_framebuffer_dimensions);
    add_if(dimensions, transformed_blocker_framebuffer_dimensions);
    add_if(textures, blocker_textures_enabled);
    add_if(textures, transformed_blocker_textures_enabled);
    add_if(depth_test, blocker_depth_test_enabled);
    add_if(depth_test, transformed_blocker_depth_test_enabled);
    add_if(depth_write, blocker_depth_write_enabled);
    add_if(depth_write, transformed_blocker_depth_write_enabled);
    add_if(stencil, blocker_stencil_enabled);
    add_if(stencil, transformed_blocker_stencil_enabled);
    add_if(blend, blocker_blending_enabled);
    add_if(blend, transformed_blocker_blending_enabled);
    add_if(alpha, blocker_alpha_test);
    add_if(alpha, transformed_blocker_alpha_test);
    add_if(logic, blocker_logic_op);
    add_if(logic, transformed_blocker_logic_op);
    add_if(color_mask, blocker_color_mask);
    add_if(color_mask, transformed_blocker_color_mask);
    add_if(cull, blocker_cull_mode);
    add_if(viewport, blocker_viewport);
    add_if(scissor, blocker_scissor);
    add_if(shadow, blocker_shadow);
    add_if(proctex, blocker_procedural_texture);

    if ((blocker_mask & (blocker_mask - 1)) != 0) {
        batches_with_multiple_blockers.fetch_add(1, std::memory_order_relaxed);
    }
}

void RecordDirectBlocker(std::uint32_t blocker_mask) {
    constexpr std::uint32_t unimplemented = 1U << 0;
    constexpr std::uint32_t topology = 1U << 1;
    constexpr std::uint32_t geometry_shader = 1U << 2;
    constexpr std::uint32_t vertex_format = 1U << 3;
    constexpr std::uint32_t index_format = 1U << 4;
    constexpr std::uint32_t other = 1U << 5;

    auto add_if = [blocker_mask](std::uint32_t bit, std::atomic<std::uint64_t>& counter) {
        if ((blocker_mask & bit) != 0) {
            counter.fetch_add(1, std::memory_order_relaxed);
        }
    };
    add_if(unimplemented, direct_blocker_unimplemented);
    add_if(topology, direct_blocker_topology);
    add_if(geometry_shader, direct_blocker_geometry_shader);
    add_if(vertex_format, direct_blocker_vertex_format);
    add_if(index_format, direct_blocker_index_format);
    add_if(other, direct_blocker_other);
}

void RecordRenderTargetCacheHit() {
    render_target_cache_hits.fetch_add(1, std::memory_order_relaxed);
}

void RecordRenderTargetCacheMiss() {
    render_target_cache_misses.fetch_add(1, std::memory_order_relaxed);
}

void RecordRenderTargetCacheCreation(std::uint64_t bytes) {
    render_target_cache_creations.fetch_add(1, std::memory_order_relaxed);
    render_target_cache_bytes.fetch_add(bytes, std::memory_order_relaxed);
}

void RecordRenderTargetCacheEviction(std::uint64_t bytes) {
    render_target_cache_evictions.fetch_add(1, std::memory_order_relaxed);
    std::uint64_t current = render_target_cache_bytes.load(std::memory_order_relaxed);
    while (current >= bytes &&
           !render_target_cache_bytes.compare_exchange_weak(current, current - bytes,
                                                            std::memory_order_relaxed)) {
    }
}

void RecordRenderTargetGpuDirty() {
    render_target_gpu_dirty.fetch_add(1, std::memory_order_relaxed);
}

void RecordRenderTargetCpuDirty() {
    render_target_cpu_dirty.fetch_add(1, std::memory_order_relaxed);
}

void RecordRenderTargetCpuDirtyByCpuMemory() {
    render_target_cpu_dirty_by_cpu_memory.fetch_add(1, std::memory_order_relaxed);
}

void RecordRenderTargetCpuDirtyBySoftware() {
    render_target_cpu_dirty_by_software.fetch_add(1, std::memory_order_relaxed);
}

void RecordRenderTargetCpuDirtyByDisplayTransfer() {
    render_target_cpu_dirty_by_display_transfer.fetch_add(1, std::memory_order_relaxed);
}

void RecordBlendState(bool supported, bool cache_hit) {
    if (supported) {
        deko_blend_state_supported.fetch_add(1, std::memory_order_relaxed);
        if (cache_hit) {
            deko_blend_pipeline_cache_hits.fetch_add(1, std::memory_order_relaxed);
        } else {
            deko_blend_pipeline_cache_misses.fetch_add(1, std::memory_order_relaxed);
        }
    } else {
        deko_blend_state_unsupported.fetch_add(1, std::memory_order_relaxed);
        unsupported_blend.fetch_add(1, std::memory_order_relaxed);
    }
}

void RecordDepthState(bool supported) {
    if (supported) {
        deko_depth_state_supported.fetch_add(1, std::memory_order_relaxed);
    } else {
        deko_depth_state_unsupported.fetch_add(1, std::memory_order_relaxed);
        unsupported_depth.fetch_add(1, std::memory_order_relaxed);
    }
}

void RecordStateSignature(std::uint64_t signature_id, bool is_new) {
    deko_state_signature_id.store(signature_id, std::memory_order_relaxed);
    if (is_new) {
        deko_state_signature_count.fetch_add(1, std::memory_order_relaxed);
    }
}

void RecordPartialBatch(std::uint64_t hw_triangles, std::uint64_t sw_triangles) {
    partial_batches.fetch_add(1, std::memory_order_relaxed);
    partial_hw_triangles.fetch_add(hw_triangles, std::memory_order_relaxed);
    partial_sw_triangles.fetch_add(sw_triangles, std::memory_order_relaxed);
}

void RecordDuplicateTrianglePrevention() {
    duplicate_triangle_preventions.fetch_add(1, std::memory_order_relaxed);
}

void RecordDroppedTriangleDetection() {
    dropped_triangle_detections.fetch_add(1, std::memory_order_relaxed);
}

void RecordPicaCommandList(std::uint64_t commands) {
    pica_command_lists_processed.fetch_add(1, std::memory_order_relaxed);
    pica_commands_processed.fetch_add(commands, std::memory_order_relaxed);
}

void RecordPicaDraw(bool indexed, std::uint64_t vertices) {
    if (indexed) {
        pica_draw_indexed_commands.fetch_add(1, std::memory_order_relaxed);
    } else {
        pica_draw_array_commands.fetch_add(1, std::memory_order_relaxed);
    }
    pica_output_vertices.fetch_add(vertices, std::memory_order_relaxed);
}

void RecordPicaOutputTriangles(std::uint64_t triangles) {
    pica_output_triangles.fetch_add(triangles, std::memory_order_relaxed);
}

void RecordPicaMemoryFill(std::uint64_t bytes, bool completed) {
    pica_memory_fill_requests.fetch_add(1, std::memory_order_relaxed);
    if (completed) {
        pica_memory_fill_completed.fetch_add(1, std::memory_order_relaxed);
        pica_memory_fill_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }
}

void RecordPicaDisplayTransfer(std::uint64_t bytes, bool completed) {
    pica_display_transfer_requests.fetch_add(1, std::memory_order_relaxed);
    if (completed) {
        pica_display_transfer_completed.fetch_add(1, std::memory_order_relaxed);
        pica_display_transfer_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }
}

void RecordPicaTextureCopy(std::uint64_t bytes, bool completed) {
    pica_texture_copy_requests.fetch_add(1, std::memory_order_relaxed);
    if (completed) {
        pica_texture_copy_completed.fetch_add(1, std::memory_order_relaxed);
        pica_texture_copy_bytes.fetch_add(bytes, std::memory_order_relaxed);
    }
}

void RecordPicaCacheFlush() {
    pica_cache_flush_requests.fetch_add(1, std::memory_order_relaxed);
}

void RecordPicaCacheInvalidation() {
    pica_cache_invalidation_requests.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspInterruptRequested() {
    gsp_interrupts_requested.fetch_add(1, std::memory_order_relaxed);
    RecordGspLogicalInterruptRaised();
}

void RecordGspInterruptDelivered() {
    gsp_interrupts_delivered.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspInterruptDropped() {
    gsp_interrupts_dropped.fetch_add(1, std::memory_order_relaxed);
    RecordGspInterruptActualDropped();
}

void RecordGspLogicalInterruptRaised() {
    gsp_logical_interrupts_raised.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspThreadDeliveryAttempt() {
    gsp_thread_delivery_attempts.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspInterruptIgnoredNoActiveThread() {
    gsp_interrupts_ignored_no_active_thread.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspInterruptIgnoredUnregisteredThread() {
    gsp_interrupts_ignored_unregistered_thread.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspInterruptIgnoredNoEvent() {
    gsp_interrupts_ignored_no_event.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspInterruptQueueFull() {
    gsp_interrupts_queue_full.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspInterruptStaleScheduledEvent() {
    gsp_interrupts_stale_scheduled_event.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspInterruptActualDropped() {
    gsp_interrupts_actual_dropped.fetch_add(1, std::memory_order_relaxed);
}

void RecordFramebufferChange(bool top, bool address, bool format, bool dimensions, bool stride) {
    if (address) {
        if (top) {
            top_framebuffer_address_changes.fetch_add(1, std::memory_order_relaxed);
        } else {
            bottom_framebuffer_address_changes.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (format) {
        framebuffer_format_changes.fetch_add(1, std::memory_order_relaxed);
    }
    if (dimensions) {
        framebuffer_dimension_changes.fetch_add(1, std::memory_order_relaxed);
    }
    if (stride) {
        framebuffer_stride_changes.fetch_add(1, std::memory_order_relaxed);
    }
}

void RecordPresent(PresentSource source, bool changed, bool top_updated, bool bottom_updated) {
    present_calls.fetch_add(1, std::memory_order_relaxed);
    if (changed) {
        present_changed_frames.fetch_add(1, std::memory_order_relaxed);
        changed_presented_frames.fetch_add(1, std::memory_order_relaxed);
    } else {
        present_unchanged_frames.fetch_add(1, std::memory_order_relaxed);
        repeated_presented_frames.fetch_add(1, std::memory_order_relaxed);
    }
    if (top_updated) {
        present_top_screen_updates.fetch_add(1, std::memory_order_relaxed);
    }
    if (bottom_updated) {
        present_bottom_screen_updates.fetch_add(1, std::memory_order_relaxed);
    }
    switch (source) {
    case PresentSource::CpuFramebufferUpload:
        present_source_cpu_upload.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::SoftwareRasterizer:
        present_source_software_rasterizer.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::DekoRenderTarget:
        present_source_deko_render_target.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::DisplayTransfer:
        present_source_display_transfer.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::MemoryFill:
        present_source_memory_fill.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::FramebufferAddressChange:
        present_source_framebuffer_change.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::CachedRenderTarget:
        present_source_cached_render_target.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::CachedRenderTargetBlit:
        present_source_cached_render_target.fetch_add(1, std::memory_order_relaxed);
        present_source_cached_render_target_blit.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::CachedRenderTargetReadback:
        present_source_cached_render_target.fetch_add(1, std::memory_order_relaxed);
        present_source_cached_render_target_readback.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::RepeatedPreviousFrame:
        present_source_repeated_frame.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::ClearFallback:
        present_source_clear_fallback.fetch_add(1, std::memory_order_relaxed);
        break;
    case PresentSource::Unknown:
        present_source_unknown.fetch_add(1, std::memory_order_relaxed);
        break;
    }
}

void RecordSystemFrame() {
    emulated_system_frames.fetch_add(1, std::memory_order_relaxed);
    emulated_vblanks.fetch_add(1, std::memory_order_relaxed);
}

void RecordGameFrame() {
    game_frame_counter.fetch_add(1, std::memory_order_relaxed);
}

void RecordHardwareRasterFrame() {
    hardware_raster_frames.fetch_add(1, std::memory_order_relaxed);
}

void RecordSoftwareRasterFrame() {
    software_raster_frames.fetch_add(1, std::memory_order_relaxed);
}

void RecordTransferOnlyFrame() {
    transfer_only_frames.fetch_add(1, std::memory_order_relaxed);
}

PerfStats TakePerfStats(PerfStats* total_stats) {
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
    stats.render_target_cache_creations = Take(render_target_cache_creations);
    stats.render_target_cache_evictions = Take(render_target_cache_evictions);
    stats.render_target_cache_bytes = render_target_cache_bytes.load(std::memory_order_relaxed);
    stats.render_target_gpu_dirty = Take(render_target_gpu_dirty);
    stats.render_target_cpu_dirty = Take(render_target_cpu_dirty);
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
#define TAKE_EXTRA_COUNTER(name) stats.name = Take(name);
    DEKO_EXTRA_COUNTERS(TAKE_EXTRA_COUNTER)
#undef TAKE_EXTRA_COUNTER
    static PerfStats lifetime{};
    AddStats(lifetime, stats);
    if (total_stats) {
        *total_stats = lifetime;
    }
    return stats;
}

PerfStats TakePerfStats() {
    return TakePerfStats(nullptr);
}

} // namespace VideoCore::Deko3D
