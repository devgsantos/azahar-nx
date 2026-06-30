// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_app.h"

#include <cstdio>
#include <exception>
#include <string>
#include "common/file_util.h"
#include "common/logging/backend.h"
#include "common/logging/log.h"
#include "common/switch_trace.h"
#include "core/core.h"
#include "network/network.h"
#include "switch_debug_log.h"
#include "switch_input.h"
#include "switch_libnx.h"
#include "switch_paths.h"
#include "switch_rom_browser.h"

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

    SWITCH_EARLY_LOG("Network::Init start");
    const bool network_ok = Network::Init();
    SWITCH_EARLY_LOGF("Network::Init end result=%s", network_ok ? "true" : "false");

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
        bool first_frame_presented = false;
        while (system.IsPoweredOn()) {
            if (loop_count < 60) {
                SWITCH_EARLY_LOGF("RunLoop iteration %d begin", loop_count);
                SWITCH_EARLY_LOGF("RunLoop iteration %d CPU running state=%s", loop_count,
                                  BoolString(system.IsPoweredOn()));
                SWITCH_EARLY_LOGF("RunLoop iteration %d renderer initialized state=%s",
                                  loop_count, BoolString(renderer_backend_available));
                SWITCH_EARLY_LOGF("RunLoop iteration %d window initialized state=%s", loop_count,
                                  BoolString(window.IsValid()));
            }
            window.PollEvents();
            const InputState input = PollInput();
            if (input.plus && input.minus) {
                LOG_INFO(Frontend, "Plus+Minus pressed; requesting shutdown");
                SWITCH_EARLY_LOG("Plus+Minus pressed; requesting shutdown");
                system.RequestShutdown();
            }

            if (loop_count < 60) {
                SWITCH_EARLY_LOGF("RunLoop iteration %d before system.RunLoop", loop_count);
            }
            const Core::System::ResultStatus run = system.RunLoop();
            if (loop_count < 60) {
                SWITCH_EARLY_LOGF("RunLoop iteration %d after system.RunLoop result=%s",
                                  loop_count, ResultToString(run));
                SWITCH_EARLY_LOGF("RunLoop iteration %d end result=%s", loop_count,
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

            const auto perf = system.GetAndResetPerfStats();
            std::printf("\x1b[1;1HAzahar Switch\nFPS %.2f  Speed %.2f%%\nPlus+Minus Exit\n",
                        perf.game_fps, perf.emulation_speed * 100.0);
            if (!first_frame_presented) {
                SWITCH_EARLY_LOG("before first frame presentation");
            }
            WaitForVBlank();
            if (!first_frame_presented) {
                SWITCH_EARLY_LOG("after first frame presentation");
                first_frame_presented = true;
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
        if (pressed.b) {
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
        if (pressed.a || pressed.b) {
            return;
        }
        WaitForVBlank();
    }
}

} // namespace Azahar::Switch

int main(int, char**) {
    Azahar::Switch::DebugLog::Initialize();
    SWITCH_EARLY_LOG("main entered");
    Azahar::Switch::SwitchApp app;
    SWITCH_EARLY_LOG("app created");
    const int result = app.Run();
    SWITCH_EARLY_LOGF("main returning %d", result);
    return result;
}
