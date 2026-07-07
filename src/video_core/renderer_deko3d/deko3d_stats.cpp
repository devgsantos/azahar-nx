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
    X(present_source_repeated_frame)                                                               \
    X(present_source_clear_fallback)                                                               \
    X(emulated_system_frames)                                                                      \
    X(emulated_vblanks)                                                                            \
    X(game_frame_counter)                                                                          \
    X(changed_presented_frames)                                                                    \
    X(repeated_presented_frames)                                                                   \
    X(hardware_raster_frames)                                                                      \
    X(software_raster_frames)                                                                      \
    X(transfer_only_frames)

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
    add_if(missing_gpu, blocker_missing_gpu_resources);
    add_if(shader, blocker_shader_unavailable);
    add_if(wrong_rt, blocker_wrong_render_target);
    add_if(format, blocker_framebuffer_format);
    add_if(dimensions, blocker_framebuffer_dimensions);
    add_if(textures, blocker_textures_enabled);
    add_if(depth_test, blocker_depth_test_enabled);
    add_if(depth_write, blocker_depth_write_enabled);
    add_if(stencil, blocker_stencil_enabled);
    add_if(blend, blocker_blending_enabled);
    add_if(alpha, blocker_alpha_test);
    add_if(logic, blocker_logic_op);
    add_if(color_mask, blocker_color_mask);
    add_if(cull, blocker_cull_mode);
    add_if(viewport, blocker_viewport);
    add_if(scissor, blocker_scissor);
    add_if(shadow, blocker_shadow);
    add_if(proctex, blocker_procedural_texture);

    if ((blocker_mask & (blocker_mask - 1)) != 0) {
        batches_with_multiple_blockers.fetch_add(1, std::memory_order_relaxed);
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
}

void RecordGspInterruptDelivered() {
    gsp_interrupts_delivered.fetch_add(1, std::memory_order_relaxed);
}

void RecordGspInterruptDropped() {
    gsp_interrupts_dropped.fetch_add(1, std::memory_order_relaxed);
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
#define TAKE_EXTRA_COUNTER(name) stats.name = Take(name);
    DEKO_EXTRA_COUNTERS(TAKE_EXTRA_COUNTER)
#undef TAKE_EXTRA_COUNTER
    return stats;
}

} // namespace VideoCore::Deko3D
