// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_app.h"

#include <chrono>
#include <cstdio>
#include <exception>
#include <string>
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "core/core.h"
#include "input_common/main.h"
#include "network/network.h"
#include "switch_debug_log.h"
#include "switch_input.h"
#include "switch_jit.h"
#include "switch_libnx.h"
#include "switch_nxlink.h"
#include "switch_paths.h"
#include "switch_rom_browser.h"
#include "video_core/renderer_deko3d/deko3d_stats.h"

namespace Azahar::Switch {
namespace {

const char* ResultToString(Core::System::ResultStatus status) {
    switch (status) {
    case Core::System::ResultStatus::Success:
        return "Success";
    case Core::System::ResultStatus::ErrorNotInitialized:
        return "Core is not initialized";
    case Core::System::ResultStatus::ErrorGetLoader:
        return "Could not identify a loader for this ROM";
    case Core::System::ResultStatus::ErrorLoader_ErrorEncrypted:
        return "Encrypted ROM or missing keys";
    case Core::System::ResultStatus::ErrorLoader_ErrorInvalidFormat:
        return "Invalid ROM format";
    case Core::System::ResultStatus::ErrorN3DSApplication:
        return "New 3DS title cannot boot in current mode";
    case Core::System::ResultStatus::ErrorRendererInit:
        return "Deko3D renderer initialization failed";
    case Core::System::ResultStatus::ShutdownRequested:
        return "Shutdown requested";
    default:
        return "Core returned an error";
    }
}

const char* BoolString(bool value) {
    return value ? "true" : "false";
}

} // namespace

bool SwitchApp::InitializeConsole() {
    if (console_active) {
        return true;
    }

    LibNx::ConsoleInit();
    console_active = true;
    SWITCH_EARLY_LOG("libnx console initialized");
    return true;
}

void SwitchApp::SuspendConsoleForRenderer() {
    if (!console_active) {
        return;
    }

    LibNx::ConsoleUpdate();
    LibNx::ConsoleExit();
    console_active = false;
    SWITCH_EARLY_LOG("libnx console released before Deko3D initialization");
}

void SwitchApp::RestoreConsoleAfterRenderer() {
    if (console_active) {
        return;
    }

    LibNx::ConsoleInit();
    console_active = true;
    SWITCH_EARLY_LOG("libnx console restored after Deko3D shutdown");
}

bool SwitchApp::InitializePlatform() {
    SWITCH_TRACE_SCOPE("Switch.Frontend", "SwitchApp::InitializePlatform");
    DebugLog::Initialize();
    SWITCH_EARLY_LOG("InitializePlatform entered");

    SWITCH_EARLY_LOG("ConsoleInit start");
    InitializeConsole();
    SWITCH_EARLY_LOG("ConsoleInit end");

    SWITCH_EARLY_LOG("RomfsInit start");
    LibNx::RomfsInit();
    SWITCH_EARLY_LOG("RomfsInit end");

    SWITCH_EARLY_LOG("InitializeFilesystemLayout start");
    const bool fs_ok = InitializeFilesystemLayout();
    SWITCH_EARLY_LOGF("InitializeFilesystemLayout end result=%s", fs_ok ? "true" : "false");

    SWITCH_EARLY_LOG("Common::Log::Initialize start");
    Common::Log::Initialize(LogPath());
    Common::Log::Start();
    SWITCH_EARLY_LOG("Common::Log::Initialize/Start end");

    SWITCH_EARLY_LOG("Switch JIT self-test start");
    const bool jit_self_test_ok = RunJitSelfTest();
    SWITCH_EARLY_LOGF("Switch JIT self-test end result=%s",
                      jit_self_test_ok ? "passed" : "failed");
    if (!jit_self_test_ok) {
        LOG_CRITICAL(Frontend, "Switch JIT self-test failed; refusing to start emulation");
        return false;
    }

    LOG_INFO(Frontend, "Switch filesystem root: {}", RootPath);
    LOG_INFO(Frontend, "Switch ROM directory: {}", RomPath);
    LOG_INFO(Frontend, "Switch ConfigDir: {}",
             FileUtil::GetUserPath(FileUtil::UserPath::ConfigDir));
    LOG_INFO(Frontend, "Switch CacheDir: {}",
             FileUtil::GetUserPath(FileUtil::UserPath::CacheDir));
    LOG_INFO(Frontend, "Switch LogDir: {}", FileUtil::GetUserPath(FileUtil::UserPath::LogDir));
    LOG_INFO(Frontend, "Switch ShaderDir: {}",
             FileUtil::GetUserPath(FileUtil::UserPath::ShaderDir));
    LOG_INFO(Frontend, "Switch NANDDir: {}", FileUtil::GetUserPath(FileUtil::UserPath::NANDDir));
    LOG_INFO(Frontend, "Switch SDMCDir: {}", FileUtil::GetUserPath(FileUtil::UserPath::SDMCDir));
    LOG_INFO(Frontend, "Switch SysDataDir: {}",
             FileUtil::GetUserPath(FileUtil::UserPath::SysDataDir));

    SWITCH_EARLY_LOG("ApplySwitchSettings start");
    ApplySwitchSettings();
    SWITCH_EARLY_LOG("ApplySwitchSettings end");

    SWITCH_EARLY_LOG("InitializeInput start");
    InitializeInput();
    SWITCH_EARLY_LOG("InitializeInput end");

    SWITCH_EARLY_LOG("InputCommon::Init start");
    InputCommon::Init();
    SWITCH_EARLY_LOG("InputCommon::Init end");

    SWITCH_EARLY_LOG("Network::Init start");
    const bool network_ok = Network::Init();
    SWITCH_EARLY_LOGF("Network::Init end result=%s", network_ok ? "true" : "false");
    SWITCH_EARLY_LOG("nxlink live stderr initialization start");
    const bool nxlink_ok = NxLink::Initialize();
    SWITCH_EARLY_LOGF("nxlink live stderr initialization result=%s",
                      nxlink_ok ? "connected" : "unavailable");
    if (nxlink_ok) {
        Common::Log::SetColorConsoleBackendEnabled(true);
        SWITCH_EARLY_LOG("nxlink live logging connected");
    }

    SWITCH_EARLY_LOG("audio.Initialize start");
    const bool audio_ok = audio.Initialize();
    SWITCH_EARLY_LOGF("audio.Initialize end result=%s", audio_ok ? "true" : "false");

    return fs_ok && network_ok;
}

int SwitchApp::Run() {
    SWITCH_TRACE_SCOPE("Switch.Frontend", "SwitchApp::Run");
    SWITCH_EARLY_LOG("SwitchApp::Run entered");
    if (!InitializePlatform()) {
        SWITCH_EARLY_LOG("InitializePlatform failed");
        Network::Shutdown();
        DrawFatal("Failed to create sdmc:/switch/azahar directory layout");
        return 1;
    }

    const ExternalDataStatus external_data = CheckExternalData();
    if (external_data.HasWarnings()) {
        DrawExternalDataWarning(external_data);
    }

    LOG_INFO(Frontend, "Azahar Switch frontend started");
    SWITCH_EARLY_LOG("Frontend main loop entered");
    while (true) {
        RomBrowser browser;
        const std::optional<std::string> game = browser.Run();
        if (!game) {
            SWITCH_EARLY_LOG("ROM browser exited by user");
            break;
        }
        const int result = LaunchGame(*game);
        SWITCH_TRACE_EVENTF("Switch.Frontend", "SwitchApp::Run", "LaunchGameResult",
                            "result=%d", result);
        SWITCH_EARLY_LOGF("LaunchGame returned %d", result);
        // Keep the frontend alive after game boot/load failure. Returning here makes the NRO close
        // and hides the real problem from the user.
        if (result != 0) {
            DrawFatal("Game launch failed. Check logs/azahar-switch-early.log.");
        }
    }

    SWITCH_EARLY_LOG("Frontend shutting down");
    audio.Shutdown();
    SWITCH_EARLY_LOG("InputCommon::Shutdown");
    InputCommon::Shutdown();
    SWITCH_EARLY_LOG("nxlink live logging shutdown");
    NxLink::Shutdown();
    Network::Shutdown();
    LibNx::RomfsExit();
    if (console_active) {
        LibNx::ConsoleExit();
        console_active = false;
    }
    Common::Log::Stop();
    SWITCH_EARLY_LOG("SwitchApp::Run returned 0");
    return 0;
}

int SwitchApp::LaunchGame(const std::string& path) {
    SWITCH_TRACE_SCOPE_DETAIL("Switch.Frontend", "SwitchApp::LaunchGame", path.c_str());
    SWITCH_EARLY_LOG("LaunchGame entered");
    SWITCH_EARLY_LOGF("selected path=%s", path.c_str());
    DrawStatus("Starting core...");

    if (!FileUtil::Exists(path)) {
        const std::string message = "Selected ROM no longer exists: " + path;
        SWITCH_EARLY_LOG(message);
        DrawFatal(message);
        return 0;
    }

    Core::System* system_ptr = nullptr;

    try {
        SWITCH_EARLY_LOG("before Core::System::GetInstance");
        Core::System& system = Core::System::GetInstance();
        system_ptr = &system;
        SWITCH_EARLY_LOG("after Core::System::GetInstance");

        DrawStatus("Loading ROM...");
        SuspendConsoleForRenderer();
        SWITCH_EARLY_LOG("before system.Load");
        SWITCH_TRACE_EVENT("Switch.Frontend", "SwitchApp::LaunchGame.system.Load", "enter");
        const Core::System::ResultStatus load = system.Load(window, path);
        SWITCH_TRACE_EVENTF("Switch.Frontend", "SwitchApp::LaunchGame.system.Load", "leave",
                            "result=%s", ResultToString(load));
        SWITCH_EARLY_LOGF("after system.Load result=%s", ResultToString(load));

        if (load != Core::System::ResultStatus::Success) {
            const std::string message = ResultToString(load);
            LOG_ERROR(Frontend, "Game load failed for {}: {}", path, message);
            SWITCH_EARLY_LOGF("Game load failed: %s", message.c_str());
            system.Shutdown();
            RestoreConsoleAfterRenderer();
            DrawFatal(message);
            return 0;
        }

        LOG_INFO(Frontend, "Emulation loop starting for {}", path);
        SWITCH_EARLY_LOG("before emulation loop");
        const bool window_valid = window.IsValid();
        const bool renderer_backend_available = IsSwitchRendererBackendAvailable();
        SWITCH_EARLY_LOGF("runtime validation window initialized state=%s", BoolString(window_valid));
        SWITCH_EARLY_LOGF("runtime validation renderer initialized state=%s",
                          BoolString(renderer_backend_available));
        SWITCH_EARLY_LOGF("runtime validation CPU running state=%s",
                          BoolString(system.IsPoweredOn()));
        if (!window_valid) {
            constexpr const char* message = "Switch EmuWindow is not initialized";
            SWITCH_EARLY_LOG(message);
            LOG_ERROR(Frontend, "{}", message);
            system.Shutdown();
            RestoreConsoleAfterRenderer();
            DrawFatal(message);
            return 0;
        }
        if (!renderer_backend_available) {
            constexpr const char* message = "Deko3D renderer not initialized";
            SWITCH_EARLY_LOG("Deko3D renderer not initialized");
            SWITCH_EARLY_LOG("returning to ROM browser");
            LOG_ERROR(Frontend, "Deko3D renderer is not initialized");
            system.Shutdown();
            RestoreConsoleAfterRenderer();
            DrawFatal(message);
            return 0;
        }

        int loop_count = 0;
        auto last_perf_sample = std::chrono::steady_clock::now();
        while (system.IsPoweredOn()) {
            if (loop_count < 10) {
                SWITCH_EARLY_LOGF("emulation loop iteration %d enter", loop_count);
            }
            window.PollEvents();
            if (loop_count < 10) {
                SWITCH_EARLY_LOGF("emulation loop iteration %d events polled", loop_count);
            }
            const InputState input = PollInput();
            if (loop_count < 10) {
                SWITCH_EARLY_LOGF("emulation loop iteration %d input polled", loop_count);
            }
            using Button = InputCommon::Switch::Button;
            if (IsButtonPressed(input, Button::Plus) && IsButtonPressed(input, Button::Minus)) {
                LOG_INFO(Frontend, "Plus+Minus pressed; requesting shutdown");
                SWITCH_EARLY_LOG("Plus+Minus pressed; requesting shutdown");
                system.RequestShutdown();
            }

            if (loop_count < 10) {
                SWITCH_EARLY_LOGF("emulation loop iteration %d before RunLoop", loop_count);
            }
            const Core::System::ResultStatus run = system.RunLoop();
            if (loop_count < 10) {
                SWITCH_EARLY_LOGF("emulation loop iteration %d after RunLoop status=%s", loop_count,
                                  ResultToString(run));
            }
            ++loop_count;

            if (run == Core::System::ResultStatus::ShutdownRequested) {
                SWITCH_EARLY_LOG("RunLoop requested shutdown");
                break;
            }
            if (run != Core::System::ResultStatus::Success) {
                const std::string message = ResultToString(run);
                LOG_ERROR(Frontend, "Emulation loop failed: {}", message);
                SWITCH_EARLY_LOGF("Emulation loop failed: %s", message.c_str());
                system.Shutdown();
                RestoreConsoleAfterRenderer();
                DrawFatal(message);
                return 0;
            }

            const auto now = std::chrono::steady_clock::now();
            if (now - last_perf_sample >= std::chrono::seconds(1)) {
                const auto perf = system.GetAndResetPerfStats();
                const auto jit_stats = TakeJitRunStats();
                const auto jit_publish_stats = TakeJitPublishStats();
                VideoCore::Deko3D::PerfStats deko_total_stats{};
                const auto deko_stats = VideoCore::Deko3D::TakePerfStats(&deko_total_stats);
                last_perf_sample = now;

                LOG_INFO(Frontend,
                         "Switch perf: FPS {:.2f} SystemFPS {:.2f} Speed {:.2f}% | "
                         "GPU {:.2f}ms Swap {:.2f}ms | HW draws={} tris={} SW fallback={} | "
                         "JIT calls={} blocks={} partial_pub={} full_pub={} | "
                         "Tex hits={} misses={}",
                         perf.game_fps, perf.system_fps, perf.emulation_speed * 100.0,
                         perf.time_gpu * 1000.0, perf.time_swap * 1000.0,
                         deko_stats.hw_draws, deko_stats.hw_triangles,
                         deko_stats.sw_fallback_draws, jit_stats.calls,
                         jit_publish_stats.blocks_compiled,
                         jit_publish_stats.partial_publishes,
                         jit_publish_stats.full_publishes,
                         deko_stats.texture_cache_hits,
                         deko_stats.texture_cache_misses);

#ifdef AZAHAR_SWITCH_PERF_DIAGNOSTICS
                SWITCH_EARLY_LOGF(
                    "performance fps=%.2f system_fps=%.2f speed=%.2f%% "
                    "vblank=%.2fms hle_svc=%.2fms hle_ipc=%.2fms gpu=%.2fms "
                    "swap=%.2fms other=%.2fms jit_ms=%.2f jit_calls=%llu "
                    "jit_req=%llu jit_exec=%llu jit_zero=%llu "
                    "jit_publish_partial=%llu jit_publish_full=%llu "
                    "jit_publish_bytes=%llu jit_publish_invalidated=%llu "
                    "jit_cache_clears=%llu jit_blocks_compiled=%llu "
                    "deko_hw_draws=%llu deko_hw_triangles=%llu "
                    "deko_hw_draw_attempts=%llu deko_hw_draw_successes=%llu "
                    "deko_hw_draw_failures=%llu deko_hw_coverage_percent=%.2f "
                    "deko_sw_fallback_draws=%llu deko_sw_fallback_triangles=%llu "
                    "deko_texture_cache_hits=%llu deko_texture_cache_misses=%llu "
                    "deko_texture_upload_bytes=%llu deko_render_target_cache_hits=%llu "
                    "deko_render_target_cache_misses=%llu deko_render_target_readbacks=%llu "
                    "deko_render_target_readback_bytes=%llu "
                    "deko_unsupported_texture_format=%llu deko_unsupported_tev=%llu "
                    "deko_unsupported_blend=%llu deko_unsupported_depth=%llu "
                    "deko_ring_waits=%llu "
                    "deko_hw_draws_submitted=%llu deko_hw_draws_completed=%llu "
                    "deko_hw_triangles_submitted=%llu deko_hw_triangles_completed=%llu "
                    "deko_fence_poll_successes=%llu deko_fence_waits=%llu "
                    "deko_fence_timeouts=%llu deko_max_fence_wait_ms=%llu "
                    "deko_queue_errors=%llu deko_queue_flushes=%llu "
                    "deko_fallback_textures_enabled=%llu deko_fallback_depth_enabled=%llu "
                    "deko_fallback_stencil_enabled=%llu deko_fallback_blend_enabled=%llu "
                    "deko_fallback_alpha_test=%llu deko_fallback_logic_op=%llu "
                    "deko_fallback_geometry_shader=%llu "
                    "deko_fallback_wrong_render_target=%llu "
                    "deko_fallback_framebuffer_format=%llu deko_fallback_topology=%llu "
                    "deko_fallback_shadow=%llu deko_fallback_unsupported_state=%llu",
                    perf.game_fps, perf.system_fps, perf.emulation_speed * 100.0,
                    perf.time_vblank_interval * 1000.0,
                    perf.time_hle_svc * 1000.0, perf.time_hle_ipc * 1000.0,
                    perf.time_gpu * 1000.0, perf.time_swap * 1000.0,
                    perf.time_remaining * 1000.0,
                    static_cast<double>(jit_stats.host_ns) / 1'000'000.0,
                    static_cast<unsigned long long>(jit_stats.calls),
                    static_cast<unsigned long long>(jit_stats.requested_ticks),
                    static_cast<unsigned long long>(jit_stats.executed_ticks),
                    static_cast<unsigned long long>(jit_stats.zero_tick_calls),
                    static_cast<unsigned long long>(jit_publish_stats.partial_publishes),
                    static_cast<unsigned long long>(jit_publish_stats.full_publishes),
                    static_cast<unsigned long long>(jit_publish_stats.bytes_flushed),
                    static_cast<unsigned long long>(jit_publish_stats.bytes_invalidated),
                    static_cast<unsigned long long>(jit_publish_stats.cache_clears),
                    static_cast<unsigned long long>(jit_publish_stats.blocks_compiled),
                    static_cast<unsigned long long>(deko_stats.hw_draws),
                    static_cast<unsigned long long>(deko_stats.hw_triangles),
                    static_cast<unsigned long long>(deko_stats.hw_draw_attempts),
                    static_cast<unsigned long long>(deko_stats.hw_draw_successes),
                    static_cast<unsigned long long>(deko_stats.hw_draw_failures),
                    deko_stats.hw_coverage_percent,
                    static_cast<unsigned long long>(deko_stats.sw_fallback_draws),
                    static_cast<unsigned long long>(deko_stats.sw_fallback_triangles),
                    static_cast<unsigned long long>(deko_stats.texture_cache_hits),
                    static_cast<unsigned long long>(deko_stats.texture_cache_misses),
                    static_cast<unsigned long long>(deko_stats.texture_upload_bytes),
                    static_cast<unsigned long long>(deko_stats.render_target_cache_hits),
                    static_cast<unsigned long long>(deko_stats.render_target_cache_misses),
                    static_cast<unsigned long long>(deko_stats.render_target_readbacks),
                    static_cast<unsigned long long>(deko_stats.render_target_readback_bytes),
                    static_cast<unsigned long long>(deko_stats.unsupported_texture_format),
                    static_cast<unsigned long long>(deko_stats.unsupported_tev),
                    static_cast<unsigned long long>(deko_stats.unsupported_blend),
                    static_cast<unsigned long long>(deko_stats.unsupported_depth),
                    static_cast<unsigned long long>(deko_stats.ring_waits),
                    static_cast<unsigned long long>(deko_stats.hw_draws_submitted),
                    static_cast<unsigned long long>(deko_stats.hw_draws_completed),
                    static_cast<unsigned long long>(deko_stats.hw_triangles_submitted),
                    static_cast<unsigned long long>(deko_stats.hw_triangles_completed),
                    static_cast<unsigned long long>(deko_stats.fence_poll_successes),
                    static_cast<unsigned long long>(deko_stats.fence_waits),
                    static_cast<unsigned long long>(deko_stats.fence_timeouts),
                    static_cast<unsigned long long>(deko_stats.max_fence_wait_ms),
                    static_cast<unsigned long long>(deko_stats.queue_errors),
                    static_cast<unsigned long long>(deko_stats.queue_flushes),
                    static_cast<unsigned long long>(deko_stats.fallback_textures_enabled),
                    static_cast<unsigned long long>(deko_stats.fallback_depth_enabled),
                    static_cast<unsigned long long>(deko_stats.fallback_stencil_enabled),
                    static_cast<unsigned long long>(deko_stats.fallback_blend_enabled),
                    static_cast<unsigned long long>(deko_stats.fallback_alpha_test),
                    static_cast<unsigned long long>(deko_stats.fallback_logic_op),
                    static_cast<unsigned long long>(deko_stats.fallback_geometry_shader),
                    static_cast<unsigned long long>(deko_stats.fallback_wrong_render_target),
                    static_cast<unsigned long long>(deko_stats.fallback_framebuffer_format),
                    static_cast<unsigned long long>(deko_stats.fallback_topology),
                    static_cast<unsigned long long>(deko_stats.fallback_shadow),
                    static_cast<unsigned long long>(deko_stats.fallback_unsupported_state));
                LOG_INFO(
                    Frontend,
                    "Switch graphics summary: interval_batch_checks={} valid={} eligible={} "
                    "submitted={} completed={} hw_tris={} rt_creations={} blend_supported={} "
                    "present_cached={} present_repeated={} raster_submit={} raster_flush={} "
                    "raster_qerr={} raster_to={} "
                    "present_qerr={} present_to={} total_batch_checks={} valid={} eligible={} "
                    "submitted={} completed={} hw_tris={} out_tris={} disp_xfers={} "
                    "presents={} changed={} repeated={} blocker_invalid={} "
                    "blocker_fb_dims={} blocker_tex={} direct_unimpl={} qerr={} to={}",
                    deko_stats.transformed_batch_checks, deko_stats.transformed_batch_valid,
                    deko_stats.transformed_batch_eligible, deko_stats.transformed_batch_submitted,
                    deko_stats.transformed_batch_completed, deko_stats.hw_triangles_completed,
                    deko_stats.render_target_cache_creations,
                    deko_stats.deko_blend_state_supported,
                    deko_stats.present_source_cached_render_target,
                    deko_stats.present_source_repeated_frame, deko_stats.raster_queue_submits,
                    deko_stats.raster_queue_flushes, deko_stats.raster_queue_errors,
                    deko_stats.raster_fence_timeouts, deko_stats.present_queue_errors,
                    deko_stats.present_fence_timeouts, deko_total_stats.transformed_batch_checks,
                    deko_total_stats.transformed_batch_valid,
                    deko_total_stats.transformed_batch_eligible,
                    deko_total_stats.transformed_batch_submitted,
                    deko_total_stats.transformed_batch_completed,
                    deko_total_stats.hw_triangles_completed, deko_total_stats.pica_output_triangles,
                    deko_total_stats.pica_display_transfer_completed,
                    deko_total_stats.present_calls, deko_total_stats.present_changed_frames,
                    deko_total_stats.present_source_repeated_frame,
                    deko_total_stats.transformed_blocker_invalid_batch,
                    deko_total_stats.transformed_blocker_framebuffer_dimensions,
                    deko_total_stats.transformed_blocker_textures_enabled,
                    deko_total_stats.direct_blocker_unimplemented, deko_total_stats.queue_errors,
                    deko_total_stats.fence_timeouts);
#ifdef AZAHAR_DEKO3D_VERBOSE_TELEMETRY
                LOG_INFO(
                    Frontend,
                    "Switch graphics telemetry: deko_transformed_batch_checks {} "
                    "deko_transformed_batch_valid {} deko_transformed_batch_invalid {} "
                    "deko_transformed_batch_eligible {} deko_transformed_batch_submitted {} "
                    "deko_transformed_batch_completed {} deko_transformed_vertices_submitted {} "
                    "deko_transformed_vertices_completed {} deko_direct_batch_checks {} "
                    "deko_direct_batch_rejected {} deko_blocker_invalid_batch {} "
                    "deko_blocker_missing_gpu_resources {} deko_blocker_shader_unavailable {} "
                    "deko_blocker_wrong_render_target {} deko_blocker_framebuffer_format {} "
                    "deko_blocker_framebuffer_dimensions {} deko_blocker_textures_enabled {} "
                    "deko_blocker_depth_test_enabled {} deko_blocker_depth_write_enabled {} "
                    "deko_blocker_stencil_enabled {} deko_blocker_blending_enabled {} "
                    "deko_blocker_alpha_test {} deko_blocker_logic_op {} "
                    "deko_blocker_color_mask {} deko_batches_with_multiple_blockers {} "
                    "deko_partial_batches {} deko_partial_hw_triangles {} "
                    "deko_partial_sw_triangles {} deko_duplicate_triangle_preventions {} "
                    "deko_dropped_triangle_detections {} pica_command_lists_processed {} "
                    "pica_commands_processed {} pica_draw_array_commands {} "
                    "pica_draw_indexed_commands {} pica_output_triangles {} "
                    "pica_output_vertices {} pica_memory_fill_requests {} "
                    "pica_memory_fill_completed {} pica_memory_fill_bytes {} "
                    "pica_display_transfer_requests {} pica_display_transfer_completed {} "
                    "pica_display_transfer_bytes {} pica_texture_copy_requests {} "
                    "pica_texture_copy_completed {} pica_texture_copy_bytes {} "
                    "pica_cache_flush_requests {} pica_cache_invalidation_requests {} "
                    "gsp_interrupts_requested {} gsp_interrupts_delivered {} "
                    "gsp_interrupts_dropped {} present_calls {} present_changed_frames {} "
                    "present_unchanged_frames {} present_source_cpu_upload {} "
                    "present_source_deko_render_target {} present_source_repeated_frame {} "
                    "emulated_system_frames {} emulated_vblanks {} game_frame_counter {} "
                    "hardware_raster_frames {} software_raster_frames {} transfer_only_frames {} "
                    "deko_raster_queue_submits {} deko_raster_queue_flushes {} "
                    "deko_raster_fence_polls {} deko_raster_fence_poll_successes {} "
                    "deko_raster_fence_waits {} deko_raster_fence_timeouts {} "
                    "deko_raster_max_fence_wait_us {} deko_raster_queue_errors {} "
                    "deko_present_queue_submits {} deko_present_queue_flushes {} "
                    "deko_present_fence_polls {} deko_present_fence_poll_successes {} "
                    "deko_present_fence_waits {} deko_present_fence_timeouts {} "
                    "deko_present_max_fence_wait_us {} deko_present_queue_errors {}",
                    deko_stats.transformed_batch_checks, deko_stats.transformed_batch_valid,
                    deko_stats.transformed_batch_invalid, deko_stats.transformed_batch_eligible,
                    deko_stats.transformed_batch_submitted,
                    deko_stats.transformed_batch_completed,
                    deko_stats.transformed_vertices_submitted,
                    deko_stats.transformed_vertices_completed, deko_stats.direct_batch_checks,
                    deko_stats.direct_batch_rejected, deko_stats.blocker_invalid_batch,
                    deko_stats.blocker_missing_gpu_resources,
                    deko_stats.blocker_shader_unavailable, deko_stats.blocker_wrong_render_target,
                    deko_stats.blocker_framebuffer_format,
                    deko_stats.blocker_framebuffer_dimensions,
                    deko_stats.blocker_textures_enabled,
                    deko_stats.blocker_depth_test_enabled,
                    deko_stats.blocker_depth_write_enabled, deko_stats.blocker_stencil_enabled,
                    deko_stats.blocker_blending_enabled, deko_stats.blocker_alpha_test,
                    deko_stats.blocker_logic_op, deko_stats.blocker_color_mask,
                    deko_stats.batches_with_multiple_blockers, deko_stats.partial_batches,
                    deko_stats.partial_hw_triangles, deko_stats.partial_sw_triangles,
                    deko_stats.duplicate_triangle_preventions,
                    deko_stats.dropped_triangle_detections,
                    deko_stats.pica_command_lists_processed, deko_stats.pica_commands_processed,
                    deko_stats.pica_draw_array_commands, deko_stats.pica_draw_indexed_commands,
                    deko_stats.pica_output_triangles, deko_stats.pica_output_vertices,
                    deko_stats.pica_memory_fill_requests, deko_stats.pica_memory_fill_completed,
                    deko_stats.pica_memory_fill_bytes, deko_stats.pica_display_transfer_requests,
                    deko_stats.pica_display_transfer_completed,
                    deko_stats.pica_display_transfer_bytes, deko_stats.pica_texture_copy_requests,
                    deko_stats.pica_texture_copy_completed, deko_stats.pica_texture_copy_bytes,
                    deko_stats.pica_cache_flush_requests,
                    deko_stats.pica_cache_invalidation_requests,
                    deko_stats.gsp_interrupts_requested, deko_stats.gsp_interrupts_delivered,
                    deko_stats.gsp_interrupts_dropped, deko_stats.present_calls,
                    deko_stats.present_changed_frames, deko_stats.present_unchanged_frames,
                    deko_stats.present_source_cpu_upload,
                    deko_stats.present_source_deko_render_target,
                    deko_stats.present_source_repeated_frame, deko_stats.emulated_system_frames,
                    deko_stats.emulated_vblanks, deko_stats.game_frame_counter,
                    deko_stats.hardware_raster_frames, deko_stats.software_raster_frames,
                    deko_stats.transfer_only_frames, deko_stats.raster_queue_submits,
                    deko_stats.raster_queue_flushes, deko_stats.raster_fence_polls,
                    deko_stats.raster_fence_poll_successes, deko_stats.raster_fence_waits,
                    deko_stats.raster_fence_timeouts, deko_stats.raster_max_fence_wait_us,
                    deko_stats.raster_queue_errors, deko_stats.present_queue_submits,
                    deko_stats.present_queue_flushes, deko_stats.present_fence_polls,
                    deko_stats.present_fence_poll_successes, deko_stats.present_fence_waits,
                    deko_stats.present_fence_timeouts, deko_stats.present_max_fence_wait_us,
                    deko_stats.present_queue_errors);
#endif
#endif
            }
        }
    } catch (const std::exception& e) {
        SWITCH_EARLY_LOGF("LaunchGame std::exception: %s", e.what());
        LOG_CRITICAL(Frontend, "LaunchGame exception: {}", e.what());
        if (system_ptr) {
            system_ptr->Shutdown();
        }
        RestoreConsoleAfterRenderer();
        DrawFatal(std::string("Launch exception: ") + e.what());
        return 0;
    } catch (...) {
        SWITCH_EARLY_LOG("LaunchGame unknown exception");
        LOG_CRITICAL(Frontend, "LaunchGame unknown exception");
        if (system_ptr) {
            system_ptr->Shutdown();
        }
        RestoreConsoleAfterRenderer();
        DrawFatal("Unknown launch exception. Check logs/azahar-switch-early.log.");
        return 0;
    }

    if (system_ptr) {
        SWITCH_EARLY_LOG("before system.Shutdown");
        system_ptr->Shutdown();
        SWITCH_EARLY_LOG("after system.Shutdown");
    }
    RestoreConsoleAfterRenderer();
    LOG_INFO(Frontend, "Emulation loop stopped");
    SWITCH_EARLY_LOG("LaunchGame normal exit");
    return 0;
}

void SwitchApp::DrawStatus(const char* message) const {
    SWITCH_EARLY_LOGF("DrawStatus: %s", message ? message : "<null>");
    std::printf("\x1b[2J\x1b[1;1HAzahar Switch\n\n%s\n", message);
    WaitForVBlank();
}

void SwitchApp::DrawFatal(const std::string& message) const {
    SWITCH_EARLY_LOGF("DrawFatal: %s", message.c_str());
    std::printf("\x1b[2J\x1b[1;1HAzahar Switch\n\nFatal error:\n%s\n\nPress B.\n",
                message.c_str());
    LOG_ERROR(Frontend, "Fatal frontend message: {}", message);
    InputState previous;
    while (true) {
        const InputState input = PollInput();
        const InputState pressed = NewlyPressed(previous, input);
        previous = input;
        if (IsButtonPressed(pressed, InputCommon::Switch::Button::B)) {
            return;
        }
        WaitForVBlank();
    }
}

void SwitchApp::DrawExternalDataWarning(const ExternalDataStatus& status) const {
    SWITCH_EARLY_LOG("DrawExternalDataWarning");
    std::printf("\x1b[2J\x1b[1;1HAzahar Switch\n\nExternal data warning\n\n");
    for (const std::string& warning : status.warnings) {
        std::printf("- %s\n", warning.c_str());
    }
    std::printf("\nSome games may fail, use fallback data, or behave incorrectly.\n");
    std::printf("A/B Continue\n");

    LOG_WARNING(Frontend, "Switch external data warnings: {}", status.warnings.size());
    for (const std::string& warning : status.warnings) {
        LOG_WARNING(Frontend, "{}", warning);
    }

    InputState previous;
    while (true) {
        const InputState input = PollInput();
        const InputState pressed = NewlyPressed(previous, input);
        previous = input;
        if (IsButtonPressed(pressed, InputCommon::Switch::Button::A) ||
            IsButtonPressed(pressed, InputCommon::Switch::Button::B)) {
            return;
        }
        WaitForVBlank();
    }
}

} // namespace Azahar::Switch

extern "C" u32 __nx_applet_exit_mode = 0;
extern "C" u64 __nx_main_thread_stack_size = 4 * 1024 * 1024;

int main(int, char**) {
    Azahar::Switch::DebugLog::Initialize();
    SWITCH_EARLY_LOG("main entered");
    Azahar::Switch::SwitchApp app;
    SWITCH_EARLY_LOG("app created");
    const int result = app.Run();
    SWITCH_EARLY_LOGF("main returning %d", result);
    return result;
}
