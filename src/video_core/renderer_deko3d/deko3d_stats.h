// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <cstdint>

namespace VideoCore::Deko3D {

enum class PresentSource {
    Unknown,
    CpuFramebufferUpload,
    SoftwareRasterizer,
    DekoRenderTarget,
    DisplayTransfer,
    MemoryFill,
    FramebufferAddressChange,
    CachedRenderTarget,
    CachedRenderTargetBlit,
    CachedRenderTargetReadback,
    RepeatedPreviousFrame,
    ClearFallback,
};

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
    std::uint64_t render_target_cache_creations = 0;
    std::uint64_t render_target_cache_evictions = 0;
    std::uint64_t render_target_cache_bytes = 0;
    std::uint64_t render_target_gpu_dirty = 0;
    std::uint64_t render_target_cpu_dirty = 0;
    std::uint64_t render_target_cpu_dirty_by_cpu_memory = 0;
    std::uint64_t render_target_cpu_dirty_by_software = 0;
    std::uint64_t render_target_cpu_dirty_by_display_transfer = 0;
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
    std::uint64_t raster_queue_submits = 0;
    std::uint64_t raster_queue_flushes = 0;
    std::uint64_t raster_fence_polls = 0;
    std::uint64_t raster_fence_poll_successes = 0;
    std::uint64_t raster_fence_waits = 0;
    std::uint64_t raster_fence_timeouts = 0;
    std::uint64_t raster_max_fence_wait_us = 0;
    std::uint64_t raster_queue_errors = 0;
    std::uint64_t present_queue_submits = 0;
    std::uint64_t present_queue_flushes = 0;
    std::uint64_t present_fence_polls = 0;
    std::uint64_t present_fence_poll_successes = 0;
    std::uint64_t present_fence_waits = 0;
    std::uint64_t present_fence_timeouts = 0;
    std::uint64_t present_max_fence_wait_us = 0;
    std::uint64_t present_queue_errors = 0;
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
    std::uint64_t fallback_invalid_transformed_batch = 0;
    std::uint64_t transformed_batch_checks = 0;
    std::uint64_t transformed_batch_valid = 0;
    std::uint64_t transformed_batch_invalid = 0;
    std::uint64_t transformed_batch_eligible = 0;
    std::uint64_t transformed_batch_submitted = 0;
    std::uint64_t transformed_batch_completed = 0;
    std::uint64_t transformed_vertices_submitted = 0;
    std::uint64_t transformed_vertices_completed = 0;
    std::uint64_t direct_batch_checks = 0;
    std::uint64_t direct_batch_rejected = 0;
    std::uint64_t transformed_blocker_invalid_batch = 0;
    std::uint64_t transformed_blocker_missing_gpu_resources = 0;
    std::uint64_t transformed_blocker_shader_unavailable = 0;
    std::uint64_t transformed_blocker_wrong_render_target = 0;
    std::uint64_t transformed_blocker_framebuffer_format = 0;
    std::uint64_t transformed_blocker_framebuffer_dimensions = 0;
    std::uint64_t transformed_blocker_textures_enabled = 0;
    std::uint64_t transformed_blocker_depth_test_enabled = 0;
    std::uint64_t transformed_blocker_depth_write_enabled = 0;
    std::uint64_t transformed_blocker_stencil_enabled = 0;
    std::uint64_t transformed_blocker_blending_enabled = 0;
    std::uint64_t transformed_blocker_alpha_test = 0;
    std::uint64_t transformed_blocker_logic_op = 0;
    std::uint64_t transformed_blocker_color_mask = 0;
    std::uint64_t direct_blocker_unimplemented = 0;
    std::uint64_t direct_blocker_topology = 0;
    std::uint64_t direct_blocker_geometry_shader = 0;
    std::uint64_t direct_blocker_vertex_format = 0;
    std::uint64_t direct_blocker_index_format = 0;
    std::uint64_t direct_blocker_other = 0;
    std::uint64_t blocker_invalid_batch = 0;
    std::uint64_t blocker_missing_gpu_resources = 0;
    std::uint64_t blocker_shader_unavailable = 0;
    std::uint64_t blocker_wrong_render_target = 0;
    std::uint64_t blocker_framebuffer_format = 0;
    std::uint64_t blocker_framebuffer_dimensions = 0;
    std::uint64_t blocker_textures_enabled = 0;
    std::uint64_t blocker_depth_test_enabled = 0;
    std::uint64_t blocker_depth_write_enabled = 0;
    std::uint64_t blocker_stencil_enabled = 0;
    std::uint64_t blocker_blending_enabled = 0;
    std::uint64_t blocker_alpha_test = 0;
    std::uint64_t blocker_logic_op = 0;
    std::uint64_t blocker_color_mask = 0;
    std::uint64_t blocker_cull_mode = 0;
    std::uint64_t blocker_viewport = 0;
    std::uint64_t blocker_scissor = 0;
    std::uint64_t blocker_shadow = 0;
    std::uint64_t blocker_procedural_texture = 0;
    std::uint64_t batches_with_multiple_blockers = 0;
    std::uint64_t partial_batches = 0;
    std::uint64_t partial_hw_triangles = 0;
    std::uint64_t partial_sw_triangles = 0;
    std::uint64_t duplicate_triangle_preventions = 0;
    std::uint64_t dropped_triangle_detections = 0;
    std::uint64_t pica_command_lists_processed = 0;
    std::uint64_t pica_commands_processed = 0;
    std::uint64_t pica_draw_array_commands = 0;
    std::uint64_t pica_draw_indexed_commands = 0;
    std::uint64_t pica_output_triangles = 0;
    std::uint64_t pica_output_vertices = 0;
    std::uint64_t pica_memory_fill_requests = 0;
    std::uint64_t pica_memory_fill_completed = 0;
    std::uint64_t pica_memory_fill_bytes = 0;
    std::uint64_t pica_display_transfer_requests = 0;
    std::uint64_t pica_display_transfer_completed = 0;
    std::uint64_t pica_display_transfer_bytes = 0;
    std::uint64_t pica_texture_copy_requests = 0;
    std::uint64_t pica_texture_copy_completed = 0;
    std::uint64_t pica_texture_copy_bytes = 0;
    std::uint64_t pica_cache_flush_requests = 0;
    std::uint64_t pica_cache_invalidation_requests = 0;
    std::uint64_t gsp_interrupts_requested = 0;
    std::uint64_t gsp_interrupts_delivered = 0;
    std::uint64_t gsp_interrupts_dropped = 0;
    std::uint64_t gsp_logical_interrupts_raised = 0;
    std::uint64_t gsp_thread_delivery_attempts = 0;
    std::uint64_t gsp_interrupts_ignored_no_active_thread = 0;
    std::uint64_t gsp_interrupts_ignored_unregistered_thread = 0;
    std::uint64_t gsp_interrupts_ignored_no_event = 0;
    std::uint64_t gsp_interrupts_queue_full = 0;
    std::uint64_t gsp_interrupts_stale_scheduled_event = 0;
    std::uint64_t gsp_interrupts_actual_dropped = 0;
    std::uint64_t top_framebuffer_address_changes = 0;
    std::uint64_t bottom_framebuffer_address_changes = 0;
    std::uint64_t framebuffer_format_changes = 0;
    std::uint64_t framebuffer_dimension_changes = 0;
    std::uint64_t framebuffer_stride_changes = 0;
    std::uint64_t present_calls = 0;
    std::uint64_t present_changed_frames = 0;
    std::uint64_t present_unchanged_frames = 0;
    std::uint64_t present_top_screen_updates = 0;
    std::uint64_t present_bottom_screen_updates = 0;
    std::uint64_t present_source_unknown = 0;
    std::uint64_t present_source_cpu_upload = 0;
    std::uint64_t present_source_software_rasterizer = 0;
    std::uint64_t present_source_deko_render_target = 0;
    std::uint64_t present_source_display_transfer = 0;
    std::uint64_t present_source_memory_fill = 0;
    std::uint64_t present_source_framebuffer_change = 0;
    std::uint64_t present_source_cached_render_target = 0;
    std::uint64_t present_source_cached_render_target_blit = 0;
    std::uint64_t present_source_cached_render_target_readback = 0;
    std::uint64_t present_source_repeated_frame = 0;
    std::uint64_t present_source_clear_fallback = 0;
    std::uint64_t emulated_system_frames = 0;
    std::uint64_t emulated_vblanks = 0;
    std::uint64_t game_frame_counter = 0;
    std::uint64_t changed_presented_frames = 0;
    std::uint64_t repeated_presented_frames = 0;
    std::uint64_t hardware_raster_frames = 0;
    std::uint64_t software_raster_frames = 0;
    std::uint64_t transfer_only_frames = 0;
    std::uint64_t deko_blend_state_supported = 0;
    std::uint64_t deko_blend_state_unsupported = 0;
    std::uint64_t deko_blend_pipeline_cache_hits = 0;
    std::uint64_t deko_blend_pipeline_cache_misses = 0;
    std::uint64_t deko_depth_state_supported = 0;
    std::uint64_t deko_depth_state_unsupported = 0;
    std::uint64_t deko_state_signature_count = 0;
    std::uint64_t deko_state_signature_id = 0;
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
void RecordRasterQueueSubmit();
void RecordRasterQueueFlush();
void RecordRasterFencePoll();
void RecordRasterFencePollSuccess();
void RecordRasterFenceWait();
void RecordRasterFenceTimeout();
void RecordRasterFenceWaitDurationUs(std::uint64_t duration_us);
void RecordRasterQueueError();
void RecordPresentQueueSubmit();
void RecordPresentQueueFlush();
void RecordPresentFencePoll();
void RecordPresentFencePollSuccess();
void RecordPresentFenceWait();
void RecordPresentFenceTimeout();
void RecordPresentFenceWaitDurationUs(std::uint64_t duration_us);
void RecordPresentQueueError();
void RecordTransformedBatchCheck(bool valid);
void RecordTransformedBatchEligible();
void RecordTransformedBatchSubmitted(std::uint64_t vertices);
void RecordTransformedBatchCompleted(std::uint64_t vertices);
void RecordDirectBatchRejected();
void RecordFallbackInvalidTransformedBatch();
void RecordBlocker(std::uint32_t blocker_mask);
void RecordTransformedBlocker(std::uint32_t blocker_mask);
void RecordDirectBlocker(std::uint32_t blocker_mask);
void RecordRenderTargetCacheHit();
void RecordRenderTargetCacheMiss();
void RecordRenderTargetCacheCreation(std::uint64_t bytes);
void RecordRenderTargetCacheEviction(std::uint64_t bytes);
void RecordRenderTargetGpuDirty();
void RecordRenderTargetCpuDirty();
void RecordRenderTargetCpuDirtyByCpuMemory();
void RecordRenderTargetCpuDirtyBySoftware();
void RecordRenderTargetCpuDirtyByDisplayTransfer();
void RecordBlendState(bool supported, bool cache_hit);
void RecordDepthState(bool supported);
void RecordStateSignature(std::uint64_t signature_id, bool is_new);
void RecordPartialBatch(std::uint64_t hw_triangles, std::uint64_t sw_triangles);
void RecordDuplicateTrianglePrevention();
void RecordDroppedTriangleDetection();
void RecordPicaCommandList(std::uint64_t commands);
void RecordPicaDraw(bool indexed, std::uint64_t vertices);
void RecordPicaOutputTriangles(std::uint64_t triangles);
void RecordPicaMemoryFill(std::uint64_t bytes, bool completed);
void RecordPicaDisplayTransfer(std::uint64_t bytes, bool completed);
void RecordPicaTextureCopy(std::uint64_t bytes, bool completed);
void RecordPicaCacheFlush();
void RecordPicaCacheInvalidation();
void RecordGspInterruptRequested();
void RecordGspInterruptDelivered();
void RecordGspInterruptDropped();
void RecordGspLogicalInterruptRaised();
void RecordGspThreadDeliveryAttempt();
void RecordGspInterruptIgnoredNoActiveThread();
void RecordGspInterruptIgnoredUnregisteredThread();
void RecordGspInterruptIgnoredNoEvent();
void RecordGspInterruptQueueFull();
void RecordGspInterruptStaleScheduledEvent();
void RecordGspInterruptActualDropped();
void RecordFramebufferChange(bool top, bool address, bool format, bool dimensions, bool stride);
void RecordPresent(PresentSource source, bool changed, bool top_updated, bool bottom_updated);
void RecordSystemFrame();
void RecordGameFrame();
void RecordHardwareRasterFrame();
void RecordSoftwareRasterFrame();
void RecordTransferOnlyFrame();
PerfStats TakePerfStats();
PerfStats TakePerfStats(PerfStats* total_stats);

} // namespace VideoCore::Deko3D
