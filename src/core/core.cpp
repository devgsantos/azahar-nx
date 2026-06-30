// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <utility>
#include <boost/serialization/array.hpp>
#include "audio_core/dsp_interface.h"
#include "audio_core/hle/hle.h"
#include "audio_core/lle/lle.h"
#include "common/arch.h"
#include "common/logging/log.h"
#include "common/settings.h"
#include "common/switch_trace.h"
#include "core/arm/arm_interface.h"
#include "core/arm/exclusive_monitor.h"
#include "core/hle/service/cam/cam.h"
#include "core/hle/service/hid/hid.h"
#include "core/hle/service/ir/ir_user.h"
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
#include "core/arm/dynarmic/arm_dynarmic.h"
#endif
#include "core/arm/dyncom/arm_dyncom.h"
#include "core/cheats/cheats.h"
#include "core/core.h"
#include "core/core_timing.h"
#include "core/dumping/backend.h"
#include "core/file_sys/ncch_container.h"
#include "core/frontend/image_interface.h"
#ifdef ENABLE_GDBSTUB
#include "core/gdbstub/gdbstub.h"
#endif
#include "core/global.h"
#include "core/hle/kernel/ipc_debugger/recorder.h"
#include "core/hle/kernel/kernel.h"
#include "core/hle/kernel/process.h"
#include "core/hle/kernel/thread.h"
#include "core/hle/service/apt/applet_manager.h"
#include "core/hle/service/apt/apt.h"
#include "core/hle/service/cam/cam.h"
#include "core/hle/service/fs/archive.h"
#include "core/hle/service/gsp/gsp.h"
#include "core/hle/service/gsp/gsp_gpu.h"
#include "core/hle/service/ir/ir_rst.h"
#include "core/hle/service/mic/mic_u.h"
#include "core/hle/service/plgldr/plgldr.h"
#include "core/hle/service/service.h"
#include "core/hle/service/sm/sm.h"
#include "core/hw/aes/key.h"
#include "core/loader/loader.h"
#include "core/movie.h"
#ifdef ENABLE_SCRIPTING
#include "core/rpc/server.h"
#endif
#include "network/network.h"
#include "video_core/custom_textures/custom_tex_manager.h"
#include "video_core/gpu.h"
#include "video_core/renderer_base.h"

namespace Core {
namespace {

void SwitchCoreEarlyLog(const char* message) {
    Common::SwitchTrace::Write("Core", "System", message);
}

void SwitchCoreEarlyLogFormat(const char* fmt, ...) {
    char buffer[1024]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    Common::SwitchTrace::WriteFormat("Core", "System", "step", "%s", buffer);
}

} // namespace

/*static*/ System System::s_instance;

template <>
Core::System& Global() {
    return System::GetInstance();
}

template <>
Kernel::KernelSystem& Global() {
    return System::GetInstance().Kernel();
}

template <>
Core::Timing& Global() {
    return System::GetInstance().CoreTiming();
}

System::System() : movie{*this}, cheat_engine{*this} {}

System::~System() = default;

System::ResultStatus System::RunLoop(bool tight_loop) {
    SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "System::RunLoop enter", "tight_loop=%s",
                        tight_loop ? "true" : "false");
    status = ResultStatus::Success;
    if (!IsPoweredOn()) {
        SWITCH_TRACE_EVENT("Core", "System::RunLoop", "powered-on validation failed");
        return ResultStatus::ErrorNotInitialized;
    }
    SWITCH_TRACE_EVENT("Core", "System::RunLoop", "powered-on validation passed");

#ifdef ENABLE_GDBSTUB
    if (GDBStub::IsServerEnabled()) {
        // The break flag is only set if GDB is connected,
        // we can do clearing here safely. If it is ever
        // used outside, move the clearing outside the if.
        for (auto& cpu_core : cpu_cores) {
            cpu_core->ClearBreakFlag();
        }
        GDBStub::HandlePacket(*this);
    }
#endif

    SWITCH_TRACE_EVENT("Core", "System::RunLoop", "signal processing enter");
    Signal signal{Signal::None};
    u32 param{};
    {
        std::scoped_lock lock{signal_mutex};
        if (current_signal != Signal::None) {
            signal = current_signal;
            param = signal_param;
            current_signal = Signal::None;
        }
    }
    switch (signal) {
    case Signal::Reset: {
        SWITCH_TRACE_EVENT("Core", "System::RunLoop", "signal reset");
        if (app_loader && app_loader->DoingInitialSetup()) {
            // Treat reset as shutdown if we are doing the initial setup
            SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
            return ResultStatus::ShutdownRequested;
        }
        Reset();
        SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
        return ResultStatus::Success;
    }
    case Signal::Shutdown:
        SWITCH_TRACE_EVENT("Core", "System::RunLoop", "signal shutdown");
        SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
        return ResultStatus::ShutdownRequested;
    case Signal::Load: {
        if (save_state_request_status != SaveStateStatus::NONE) {
            LOG_ERROR(Core, "A pending save state operation has not finished yet");
            status_details = "A pending save state operation has not finished yet";
            SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
            return ResultStatus::ErrorSavestate;
        }
        save_state_slot = param;
        save_state_request_time = std::chrono::steady_clock::now();
        save_state_request_status = SaveStateStatus::LOADING;
        break;
    }
    case Signal::Save: {
        if (save_state_request_status != SaveStateStatus::NONE) {
            LOG_ERROR(Core, "A pending save state operation has not finished yet");
            status_details = "A pending save state operation has not finished yet";
            SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
            return ResultStatus::ErrorSavestate;
        }
        save_state_slot = param;
        save_state_request_time = std::chrono::steady_clock::now();
        save_state_request_status = SaveStateStatus::SAVING;
        break;
    }
    default:
        break;
    }
    SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "signal processing passed", "signal=%d",
                        static_cast<int>(signal));

    SWITCH_TRACE_EVENT("Core", "System::RunLoop", "save-state processing enter");
    if (save_state_request_status == SaveStateStatus::LOADING && kernel.get() &&
        !kernel->AreAsyncOperationsPending()) {
        const u32 slot = save_state_slot;
        save_state_request_status = SaveStateStatus::NONE;
        LOG_INFO(Core, "Begin load of slot {}", slot);
        try {
            System::LoadState(slot);
            LOG_INFO(Core, "Load completed");
        } catch (const std::exception& e) {
            LOG_ERROR(Core, "Error loading: {}", e.what());
            status_details = e.what();
            SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
            return ResultStatus::ErrorSavestate;
        }
        frame_limiter.WaitOnce();
        SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
        return ResultStatus::Success;
    } else if (save_state_request_status == SaveStateStatus::SAVING && kernel.get() &&
               !kernel->AreAsyncOperationsPending()) {
        save_state_request_status = SaveStateStatus::NONE;
        const u32 slot = save_state_slot;
        LOG_INFO(Core, "Begin save to slot {}", slot);
        try {
            System::SaveState(slot);
            LOG_INFO(Core, "Save completed");
        } catch (const std::exception& e) {
            LOG_ERROR(Core, "Error saving: {}", e.what());
            status_details = e.what();
            SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
            return ResultStatus::ErrorSavestate;
        }
        frame_limiter.WaitOnce();
        SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
        return ResultStatus::Success;
    } else if (save_state_request_status != SaveStateStatus::NONE &&
               (std::chrono::steady_clock::now() - save_state_request_time) >
                   std::chrono::seconds(5)) {
        save_state_request_status = SaveStateStatus::NONE;
        LOG_ERROR(Core, "Cannot perform save state operation due to pending async operations");
        status_details = "Cannot perform save state operation due to pending async operations";
        SWITCH_TRACE_EVENT("Core", "System::RunLoop", "System::RunLoop leave");
        return ResultStatus::ErrorSavestate;
    }
    SWITCH_TRACE_EVENT("Core", "System::RunLoop", "save-state processing passed");

    // All cores should have executed the same amount of ticks. If this is not the case an event was
    // scheduled with a cycles_into_future smaller then the current downcount.
    // So we have to get those cores to the same global time first
    SWITCH_TRACE_EVENT("Core", "System::RunLoop", "GetGlobalTicks enter");
    u64 global_ticks = timing->GetGlobalTicks();
    SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "GetGlobalTicks leave", "ticks=%llu",
                        static_cast<unsigned long long>(global_ticks));
    s64 max_delay = 0;
    ARM_Interface* current_core_to_execute = nullptr;
    SWITCH_TRACE_EVENT("Core", "System::RunLoop", "core synchronization enter");
    for (auto& cpu_core : cpu_cores) {
        SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "core synchronization inspect",
                            "core=%u ticks=%llu downcount=%lld", cpu_core->GetID(),
                            static_cast<unsigned long long>(cpu_core->GetTimer().GetTicks()),
                            static_cast<long long>(cpu_core->GetTimer().GetDowncount()));
        if (cpu_core->GetTimer().GetTicks() < global_ticks) {
            s64 delay = global_ticks - cpu_core->GetTimer().GetTicks();
            running_core = cpu_core.get();
            kernel->SetRunningCPU(running_core);
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "timer Advance enter",
                                "core=%u delay=%lld", cpu_core->GetID(),
                                static_cast<long long>(delay));
            cpu_core->GetTimer().Advance();
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "timer Advance leave",
                                "core=%u ticks=%llu downcount=%lld", cpu_core->GetID(),
                                static_cast<unsigned long long>(cpu_core->GetTimer().GetTicks()),
                                static_cast<long long>(cpu_core->GetTimer().GetDowncount()));
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "PrepareReschedule enter", "core=%u",
                                cpu_core->GetID());
            cpu_core->PrepareReschedule();
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "PrepareReschedule leave", "core=%u",
                                cpu_core->GetID());
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop",
                                "ThreadManager::Reschedule enter", "core=%u",
                                cpu_core->GetID());
            kernel->GetThreadManager(cpu_core->GetID()).Reschedule();
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop",
                                "ThreadManager::Reschedule leave", "core=%u",
                                cpu_core->GetID());
            cpu_core->GetTimer().SetNextSlice(delay);
            if (max_delay < delay) {
                max_delay = delay;
                current_core_to_execute = cpu_core.get();
            }
        }
    }
    SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "core synchronization leave",
                        "max_delay=%lld selected_core=%d", static_cast<long long>(max_delay),
                        current_core_to_execute ? static_cast<int>(current_core_to_execute->GetID())
                                                : -1);

    // jit sometimes overshoot by a few ticks which might lead to a minimal desync in the cores.
    // This small difference shouldn't make it necessary to sync the cores and would only cost
    // performance. Thus we don't sync delays below min_delay
    static constexpr s64 min_delay = 100;
    if (max_delay > min_delay) {
        LOG_TRACE(Core_ARM11, "Core {} running (delayed) for {} ticks",
                  current_core_to_execute->GetID(),
                  current_core_to_execute->GetTimer().GetDowncount());
        if (running_core != current_core_to_execute) {
            running_core = current_core_to_execute;
            kernel->SetRunningCPU(running_core);
        }
        SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "current thread lookup enter", "core=%u",
                            current_core_to_execute->GetID());
        if (kernel->GetCurrentThreadManager().GetCurrentThread() == nullptr) {
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "current thread lookup leave",
                                "core=%u has_thread=false",
                                current_core_to_execute->GetID());
            LOG_TRACE(Core_ARM11, "Core {} idling", current_core_to_execute->GetID());
            current_core_to_execute->GetTimer().Idle();
            SWITCH_TRACE_EVENT("Core", "System::RunLoop", "before Reschedule");
            PrepareReschedule();
            SWITCH_TRACE_EVENT("Core", "System::RunLoop", "after Reschedule");
        } else {
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "current thread lookup leave",
                                "core=%u has_thread=true",
                                current_core_to_execute->GetID());
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "before cpu_core->Run or Step",
                                "core=%u mode=%s pc=0x%08x downcount=%lld",
                                current_core_to_execute->GetID(),
                                tight_loop ? "Run" : "Step", current_core_to_execute->GetPC(),
                                static_cast<long long>(
                                    current_core_to_execute->GetTimer().GetDowncount()));
            if (tight_loop) {
                current_core_to_execute->Run();
            } else {
                current_core_to_execute->Step();
            }
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "after cpu_core->Run or Step",
                                "core=%u pc=0x%08x ticks=%llu downcount=%lld",
                                current_core_to_execute->GetID(), current_core_to_execute->GetPC(),
                                static_cast<unsigned long long>(
                                    current_core_to_execute->GetTimer().GetTicks()),
                                static_cast<long long>(
                                    current_core_to_execute->GetTimer().GetDowncount()));
        }
    } else {
        // Now all cores are at the same global time. So we will run them one after the other
        // with a max slice that is the minimum of all max slices of all cores
        // TODO: Make special check for idle since we can easily revert the time of idle cores
        s64 max_slice = Timing::MAX_SLICE_LENGTH;
        for (const auto& cpu_core : cpu_cores) {
            running_core = cpu_core.get();
            kernel->SetRunningCPU(running_core);
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "timer Advance enter", "core=%u",
                                cpu_core->GetID());
            cpu_core->GetTimer().Advance();
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "timer Advance leave",
                                "core=%u ticks=%llu downcount=%lld", cpu_core->GetID(),
                                static_cast<unsigned long long>(cpu_core->GetTimer().GetTicks()),
                                static_cast<long long>(cpu_core->GetTimer().GetDowncount()));
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "PrepareReschedule enter", "core=%u",
                                cpu_core->GetID());
            cpu_core->PrepareReschedule();
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "PrepareReschedule leave", "core=%u",
                                cpu_core->GetID());
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop",
                                "ThreadManager::Reschedule enter", "core=%u",
                                cpu_core->GetID());
            kernel->GetThreadManager(cpu_core->GetID()).Reschedule();
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop",
                                "ThreadManager::Reschedule leave", "core=%u",
                                cpu_core->GetID());
            max_slice = std::min(max_slice, cpu_core->GetTimer().GetMaxSliceLength());
        }
        for (auto& cpu_core : cpu_cores) {
            cpu_core->GetTimer().SetNextSlice(max_slice);
            auto start_ticks = cpu_core->GetTimer().GetTicks();
            LOG_TRACE(Core_ARM11, "Core {} running for {} ticks", cpu_core->GetID(),
                      cpu_core->GetTimer().GetDowncount());
            running_core = cpu_core.get();
            kernel->SetRunningCPU(running_core);
            // If we don't have a currently active thread then don't execute instructions,
            // instead advance to the next event and try to yield to the next thread
            SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "current thread lookup enter",
                                "core=%u", cpu_core->GetID());
            if (kernel->GetCurrentThreadManager().GetCurrentThread() == nullptr) {
                SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "current thread lookup leave",
                                    "core=%u has_thread=false", cpu_core->GetID());
                LOG_TRACE(Core_ARM11, "Core {} idling", cpu_core->GetID());
                cpu_core->GetTimer().Idle();
                SWITCH_TRACE_EVENT("Core", "System::RunLoop", "before Reschedule");
                PrepareReschedule();
                SWITCH_TRACE_EVENT("Core", "System::RunLoop", "after Reschedule");
            } else {
                SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "current thread lookup leave",
                                    "core=%u has_thread=true", cpu_core->GetID());
                // In the rare case the break flag is set (due to exception thrown)
                // there is probably no need to adjust the timer accordingly.
                SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "before cpu_core->Run or Step",
                                    "core=%u mode=%s pc=0x%08x downcount=%lld",
                                    cpu_core->GetID(), tight_loop ? "Run" : "Step",
                                    cpu_core->GetPC(),
                                    static_cast<long long>(cpu_core->GetTimer().GetDowncount()));
                if (tight_loop) {
                    cpu_core->Run();
                } else {
                    cpu_core->Step();
                }
                SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "after cpu_core->Run or Step",
                                    "core=%u pc=0x%08x ticks=%llu downcount=%lld",
                                    cpu_core->GetID(), cpu_core->GetPC(),
                                    static_cast<unsigned long long>(
                                        cpu_core->GetTimer().GetTicks()),
                                    static_cast<long long>(cpu_core->GetTimer().GetDowncount()));
            }
            max_slice = cpu_core->GetTimer().GetTicks() - start_ticks;
        }
    }

    SWITCH_TRACE_EVENT("Core", "System::RunLoop", "before Reschedule");
    Reschedule();
    SWITCH_TRACE_EVENT("Core", "System::RunLoop", "after Reschedule");

    SWITCH_TRACE_EVENTF("Core", "System::RunLoop", "System::RunLoop leave", "status=%d",
                        static_cast<int>(status));
    return status;
}

bool System::SendSignal(System::Signal signal, u32 param) {
    std::scoped_lock lock{signal_mutex};
    if (current_signal != signal && current_signal != Signal::None) {
        LOG_ERROR(Core, "Unable to {} as {} is ongoing", signal, current_signal);
        return false;
    }
    current_signal = signal;
    signal_param = param;
    return true;
}

System::ResultStatus System::SingleStep() {
    return RunLoop(false);
}

System::ResultStatus System::Load(Frontend::EmuWindow& emu_window, const std::string& filepath,
                                  Frontend::EmuWindow* secondary_window) {
    SwitchCoreEarlyLogFormat("Core::System::Load entered filepath=%s", filepath.c_str());
    SwitchCoreEarlyLog("Core::System::Load ResetTemporaryFrameLimit enter");
    Settings::ResetTemporaryFrameLimit();
    SwitchCoreEarlyLog("Core::System::Load ResetTemporaryFrameLimit leave");
    SwitchCoreEarlyLog("Core::System::Load SetCurrentRomPath enter");
    FileUtil::SetCurrentRomPath(filepath);
    SwitchCoreEarlyLog("Core::System::Load SetCurrentRomPath leave");
    if (early_app_loader) {
        SwitchCoreEarlyLog("Core::System::Load using early_app_loader");
        app_loader = std::move(early_app_loader);
    } else {
        SwitchCoreEarlyLog("Core::System::Load GetLoader enter");
        app_loader = Loader::GetLoader(filepath);
        SwitchCoreEarlyLogFormat("Core::System::Load GetLoader leave loader=%s",
                                 app_loader ? "true" : "false");
    }
    if (!app_loader) {
        LOG_CRITICAL(Core, "Failed to obtain loader for {}", filepath);
        SwitchCoreEarlyLog("Core::System::Load failed no loader");
        return ResultStatus::ErrorGetLoader;
    }

    u64_le program_id = 0;
    SwitchCoreEarlyLog("Core::System::Load ReadProgramId enter");
    app_loader->ReadProgramId(program_id);
    SwitchCoreEarlyLogFormat("Core::System::Load ReadProgramId leave program_id=0x%llx",
                             static_cast<unsigned long long>(program_id));
    if (restore_plugin_context.has_value() && restore_plugin_context->is_enabled &&
        restore_plugin_context->use_user_load_parameters) {
        if (restore_plugin_context->user_load_parameters.low_title_Id ==
                static_cast<u32_le>(program_id) &&
            restore_plugin_context->user_load_parameters.plugin_memory_strategy ==
                Service::PLGLDR::PLG_LDR::PluginMemoryStrategy::PLG_STRATEGY_MODE3) {
            app_loader->SetKernelMemoryModeOverride(Kernel::MemoryMode::Dev2);
        }
    }

    Kernel::MemoryMode app_mem_mode;
    Kernel::MemoryMode system_mem_mode;
    bool used_default_mem_mode = false;
    Kernel::New3dsHwCapabilities app_n3ds_hw_capabilities;

    if (m_mem_mode) {
        SwitchCoreEarlyLog("Core::System::Load using FIRM memory mode override");
        // Use memory mode set by the FIRM launch parameters
        system_mem_mode = static_cast<Kernel::MemoryMode>(m_mem_mode.value());
        m_mem_mode = {};
    } else {
        SwitchCoreEarlyLog("Core::System::Load using default memory mode");
        // Use default memory mode based on the n3ds setting
        system_mem_mode = Settings::values.is_new_3ds.GetValue() ? Kernel::MemoryMode::NewProd
                                                                 : Kernel::MemoryMode::Prod;
        used_default_mem_mode = true;
    }

    {
        SwitchCoreEarlyLog("Core::System::Load LoadKernelMemoryMode enter");
        auto memory_mode = app_loader->LoadKernelMemoryMode();
        SwitchCoreEarlyLogFormat("Core::System::Load LoadKernelMemoryMode leave status=%d has=%s",
                                 static_cast<int>(memory_mode.second),
                                 memory_mode.first ? "true" : "false");
        if (memory_mode.second != Loader::ResultStatus::Success) {
            LOG_CRITICAL(Core, "Failed to determine system mode (Error {})!",
                         static_cast<int>(memory_mode.second));

            switch (memory_mode.second) {
            case Loader::ResultStatus::ErrorEncrypted:
                return ResultStatus::ErrorLoader_ErrorEncrypted;
            case Loader::ResultStatus::ErrorInvalidFormat:
                return ResultStatus::ErrorLoader_ErrorInvalidFormat;
            case Loader::ResultStatus::ErrorGbaTitle:
                return ResultStatus::ErrorLoader_ErrorGbaTitle;
            case Loader::ResultStatus::ErrorArtic:
                return ResultStatus::ErrorArticDisconnected;
            default:
                return ResultStatus::ErrorSystemMode;
            }
        }

        ASSERT(memory_mode.first);
        app_mem_mode = memory_mode.first.value();
    }

    SwitchCoreEarlyLog("Core::System::Load LoadNew3dsHwCapabilities enter");
    auto n3ds_hw_caps = app_loader->LoadNew3dsHwCapabilities();
    SwitchCoreEarlyLogFormat("Core::System::Load LoadNew3dsHwCapabilities leave status=%d has=%s",
                             static_cast<int>(n3ds_hw_caps.second),
                             n3ds_hw_caps.first ? "true" : "false");
    ASSERT(n3ds_hw_caps.first);
    app_n3ds_hw_capabilities = n3ds_hw_caps.first.value();

    SwitchCoreEarlyLog("Core::System::Load IsN3DSExclusive enter");
    if (!Settings::values.is_new_3ds.GetValue() && app_loader->IsN3DSExclusive()) {
        SwitchCoreEarlyLog("Core::System::Load IsN3DSExclusive rejected");
        return ResultStatus::ErrorN3DSApplication;
    }
    SwitchCoreEarlyLog("Core::System::Load IsN3DSExclusive leave");

    // If the default mem mode has been used, we do not come from a FIRM launch. On real HW
    // however, the home menu is in charge or setting the proper memory mode when launching
    // applications by doing a FIRM launch. Since we launch the application without going
    // through the home menu, we need to emulate the FIRM launch having happened and set the
    // proper memory mode.
    if (used_default_mem_mode) {

        // If we are on the Old 3DS prod mode and the application memory mode does not match, we
        // need to adjust it. We do not need adjustment if we are on the New 3DS prod mode, as that
        // one overrides all the Old 3DS memory modes.
        if (system_mem_mode == Kernel::MemoryMode::Prod && app_mem_mode != system_mem_mode) {
            system_mem_mode = app_mem_mode;
        }

        // If we are on the New 3DS prod mode, and the application needs the New 3DS extended
        // memory mode (only CTRAging is known to do this), adjust the memory mode.
        else if (system_mem_mode == Kernel::MemoryMode::NewProd &&
                 app_n3ds_hw_capabilities.memory_mode == Kernel::New3dsMemoryMode::NewDev1) {
            system_mem_mode = Kernel::MemoryMode::NewDev1;
        }
    }

    u32 num_cores = 2;
    if (Settings::values.is_new_3ds) {
        num_cores = 4;
    }
    SwitchCoreEarlyLogFormat("Core::System::Load Init enter system_mem_mode=%d app_mem_mode=%d "
                             "num_cores=%u",
                             static_cast<int>(system_mem_mode), static_cast<int>(app_mem_mode),
                             num_cores);
    ResultStatus init_result{Init(emu_window, secondary_window, system_mem_mode, num_cores)};
    SwitchCoreEarlyLogFormat("Core::System::Load Init leave status=%d",
                             static_cast<int>(init_result));
    if (init_result != ResultStatus::Success) {
        LOG_CRITICAL(Core, "Failed to initialize system (Error {})!",
                     static_cast<u32>(init_result));
        System::Shutdown();
        return init_result;
    }

    SwitchCoreEarlyLog("Core::System::Load UpdateCPUAndMemoryState enter");
    kernel->UpdateCPUAndMemoryState(program_id, app_mem_mode, app_n3ds_hw_capabilities);
    SwitchCoreEarlyLog("Core::System::Load UpdateCPUAndMemoryState leave");

    // Restore any parameters that should be carried through a reset.
    if (auto apt = Service::APT::GetModule(*this)) {
        if (restore_deliver_arg.has_value()) {
            apt->GetAppletManager()->SetDeliverArg(restore_deliver_arg);
            restore_deliver_arg.reset();
        }
        if (restore_sys_menu_arg.has_value()) {
            apt->GetAppletManager()->SetSysMenuArg(restore_sys_menu_arg.value());
            restore_sys_menu_arg.reset();
        }
        apt->SetWirelessRebootInfoBuffer(restore_wireless_reboot_info);
    }

    if (restore_plugin_context.has_value()) {
        if (auto plg_ldr = Service::PLGLDR::GetService(*this)) {
            plg_ldr->SetPluginLoaderContext(restore_plugin_context.value());
        }
        restore_plugin_context.reset();
    }

    if (restore_ipc_recorder) {
        kernel->RestoreIPCRecorder(std::move(restore_ipc_recorder));
    }

    std::shared_ptr<Kernel::Process> process;
    SwitchCoreEarlyLog("Core::System::Load app_loader->Load enter");
    const Loader::ResultStatus load_result{app_loader->Load(process)};
    SwitchCoreEarlyLogFormat("Core::System::Load app_loader->Load leave status=%d process=%s",
                             static_cast<int>(load_result), process ? "true" : "false");
    if (Loader::ResultStatus::Success != load_result) {
        LOG_CRITICAL(Core, "Failed to load ROM (Error {})!", load_result);
        System::Shutdown();

        switch (load_result) {
        case Loader::ResultStatus::ErrorEncrypted:
            return ResultStatus::ErrorLoader_ErrorEncrypted;
        case Loader::ResultStatus::ErrorInvalidFormat:
            return ResultStatus::ErrorLoader_ErrorInvalidFormat;
        case Loader::ResultStatus::ErrorGbaTitle:
            return ResultStatus::ErrorLoader_ErrorGbaTitle;
        case Loader::ResultStatus::ErrorPatches:
            return ResultStatus::ErrorLoader_ErrorPatches;
        case Loader::ResultStatus::ErrorPatchesInvalidTitle:
            return ResultStatus::ErrorLoader_ErrorPatchesInvalidTitle;
        case Loader::ResultStatus::ErrorArtic:
            return ResultStatus::ErrorArticDisconnected;
        default:
            return ResultStatus::ErrorLoader;
        }
    }
    SwitchCoreEarlyLog("Core::System::Load SetCurrentProcess enter");
    kernel->SetCurrentProcess(process);
    SwitchCoreEarlyLog("Core::System::Load SetCurrentProcess leave");
    title_id = 0;
    SwitchCoreEarlyLog("Core::System::Load ReadProgramId final enter");
    if (app_loader->ReadProgramId(title_id) != Loader::ResultStatus::Success) {
        LOG_ERROR(Core, "Failed to find title id for ROM (Error {})",
                  static_cast<u32>(load_result));
    }
    SwitchCoreEarlyLogFormat("Core::System::Load ReadProgramId final leave title_id=0x%llx",
                             static_cast<unsigned long long>(title_id));

    SwitchCoreEarlyLog("Core::System::Load CheatEngine enter");
    cheat_engine.LoadCheatFile(title_id);
    cheat_engine.Connect(process->process_id);
    SwitchCoreEarlyLog("Core::System::Load CheatEngine leave");

    SwitchCoreEarlyLog("Core::System::Load PerfStats enter");
    perf_stats = std::make_unique<PerfStats>(title_id);
    SwitchCoreEarlyLog("Core::System::Load PerfStats leave");

    if (Settings::values.dump_textures) {
        SwitchCoreEarlyLog("Core::System::Load PrepareDumping enter");
        custom_tex_manager->PrepareDumping(title_id);
        SwitchCoreEarlyLog("Core::System::Load PrepareDumping leave");
    }
    if (Settings::values.custom_textures) {
        SwitchCoreEarlyLog("Core::System::Load FindCustomTextures enter");
        custom_tex_manager->FindCustomTextures();
        SwitchCoreEarlyLog("Core::System::Load FindCustomTextures leave");
    }

    status = ResultStatus::Success;
    m_emu_window = &emu_window;
    m_secondary_window = secondary_window;
    m_filepath = filepath;

    // Reset counters and set time origin to current frame
    SwitchCoreEarlyLog("Core::System::Load BeginSystemFrame enter");
    [[maybe_unused]] const PerfStats::Results result = GetAndResetPerfStats();
    perf_stats->BeginSystemFrame();
    SwitchCoreEarlyLog("Core::System::Load BeginSystemFrame leave");
    SwitchCoreEarlyLog("Core::System::Load leave success");
    return status;
}

void System::PrepareReschedule() {
    running_core->PrepareReschedule();
    reschedule_pending = true;
}

PerfStats::Results System::GetAndResetPerfStats() {
    return (perf_stats && timing) ? perf_stats->GetAndResetStats(timing->GetGlobalTimeUs())
                                  : PerfStats::Results{};
}

PerfStats::Results System::GetLastPerfStats() {
    return perf_stats ? perf_stats->GetLastStats() : PerfStats::Results{};
}

double System::GetStableFrameTimeScale() {
    return perf_stats->GetStableFrameTimeScale();
}

void System::Reschedule() {
    if (!reschedule_pending) {
        return;
    }

    reschedule_pending = false;
    for (const auto& core : cpu_cores) {
        LOG_TRACE(Core_ARM11, "Reschedule core {}", core->GetID());
        kernel->GetThreadManager(core->GetID()).Reschedule();
    }
}

System::ResultStatus System::Init(Frontend::EmuWindow& emu_window,
                                  Frontend::EmuWindow* secondary_window,
                                  Kernel::MemoryMode memory_mode, u32 num_cores) {
    SWITCH_TRACE_EVENTF("Core", "System::Init", "enter", "memory_mode=%d num_cores=%u",
                        static_cast<int>(memory_mode), num_cores);
    LOG_DEBUG(HW_Memory, "initialized OK");

    SWITCH_TRACE_EVENT("Core", "System::Init.MemorySystem", "enter");
    memory = std::make_unique<Memory::MemorySystem>(*this);
    SWITCH_TRACE_EVENT("Core", "System::Init.MemorySystem", "leave");

    SWITCH_TRACE_EVENT("Core", "System::Init.Timing", "enter");
    timing = std::make_unique<Timing>(num_cores, Settings::values.cpu_clock_percentage.GetValue(),
                                      movie.GetOverrideBaseTicks());
    SWITCH_TRACE_EVENT("Core", "System::Init.Timing", "leave");

    SWITCH_TRACE_EVENT("Core", "System::Init.KernelSystem", "enter");
    kernel = std::make_unique<Kernel::KernelSystem>(
        *memory, *timing, [this] { PrepareReschedule(); }, memory_mode, num_cores,
        movie.GetOverrideInitTime());
    SWITCH_TRACE_EVENT("Core", "System::Init.KernelSystem", "leave");

    SWITCH_TRACE_EVENT("Core", "System::Init.ExclusiveMonitor", "enter");
    exclusive_monitor = MakeExclusiveMonitor(*memory, num_cores);
    SWITCH_TRACE_EVENT("Core", "System::Init.ExclusiveMonitor", "leave");
    cpu_cores.reserve(num_cores);
    if (Settings::values.use_cpu_jit) {
#if CITRA_ARCH(x86_64) || CITRA_ARCH(arm64)
        for (u32 i = 0; i < num_cores; ++i) {
            SWITCH_TRACE_EVENTF("Core", "System::Init.CPU", "enter", "core=%u backend=Dynarmic",
                                i);
            cpu_cores.push_back(std::make_shared<ARM_Dynarmic>(
                *this, *memory, i, timing->GetTimer(i), *exclusive_monitor));
            SWITCH_TRACE_EVENTF("Core", "System::Init.CPU", "leave", "core=%u backend=Dynarmic",
                                i);
        }
#else
        for (u32 i = 0; i < num_cores; ++i) {
            SWITCH_TRACE_EVENTF("Core", "System::Init.CPU", "enter", "core=%u backend=DynCom",
                                i);
            cpu_cores.push_back(
                std::make_shared<ARM_DynCom>(*this, *memory, USER32MODE, i, timing->GetTimer(i)));
            SWITCH_TRACE_EVENTF("Core", "System::Init.CPU", "leave", "core=%u backend=DynCom",
                                i);
        }
        LOG_WARNING(Core, "CPU JIT requested, but Dynarmic not available");
#endif
    } else {
        for (u32 i = 0; i < num_cores; ++i) {
            SWITCH_TRACE_EVENTF("Core", "System::Init.CPU", "enter", "core=%u backend=DynCom",
                                i);
            cpu_cores.push_back(
                std::make_shared<ARM_DynCom>(*this, *memory, USER32MODE, i, timing->GetTimer(i)));
            SWITCH_TRACE_EVENTF("Core", "System::Init.CPU", "leave", "core=%u backend=DynCom",
                                i);
        }
    }
    SWITCH_TRACE_EVENT("Core", "System::Init.SetRunningCPU", "enter");
    running_core = cpu_cores[0].get();

    kernel->SetCPUs(cpu_cores);
    kernel->SetRunningCPU(cpu_cores[0].get());
    SWITCH_TRACE_EVENT("Core", "System::Init.SetRunningCPU", "leave");

    const auto audio_emulation = Settings::values.audio_emulation.GetValue();
    SWITCH_TRACE_EVENTF("Core", "System::Init.DSP", "enter", "audio_emulation=%d",
                        static_cast<int>(audio_emulation));
    if (audio_emulation == Settings::AudioEmulation::HLE) {
        dsp_core = std::make_unique<AudioCore::DspHle>(*this);
    } else {
        const bool multithread = audio_emulation == Settings::AudioEmulation::LLEMultithreaded;
        dsp_core = std::make_unique<AudioCore::DspLle>(*this, multithread);
    }
    SWITCH_TRACE_EVENT("Core", "System::Init.DSP", "leave");

    SWITCH_TRACE_EVENT("Core", "System::Init.DSPSettings", "enter");
    dsp_core->SetSink(Settings::values.output_type.GetValue(),
                      Settings::values.output_device.GetValue());
    dsp_core->EnableStretching(Settings::values.enable_audio_stretching.GetValue());
    SWITCH_TRACE_EVENT("Core", "System::Init.DSPSettings", "leave");

#ifdef ENABLE_SCRIPTING
    if (Settings::values.enable_rpc_server.GetValue()) {
        rpc_server = std::make_unique<RPC::Server>(*this);
    }
#endif

    SWITCH_TRACE_EVENT("Core", "System::Init.ServiceManager", "enter");
    service_manager = std::make_unique<Service::SM::ServiceManager>(*this);
    SWITCH_TRACE_EVENT("Core", "System::Init.ServiceManager", "leave");
    SWITCH_TRACE_EVENT("Core", "System::Init.ArchiveManager", "enter");
    archive_manager = std::make_unique<Service::FS::ArchiveManager>(*this);
    SWITCH_TRACE_EVENT("Core", "System::Init.ArchiveManager", "leave");

    u64 loading_title_id = 0;
    SWITCH_TRACE_EVENT("Core", "System::Init.ReadProgramId", "enter");
    app_loader->ReadProgramId(loading_title_id);
    SWITCH_TRACE_EVENTF("Core", "System::Init.ReadProgramId", "leave", "title_id=0x%llx",
                        static_cast<unsigned long long>(loading_title_id));
    SWITCH_TRACE_EVENT("Core", "System::Init.AESInitKeys", "enter");
    HW::AES::InitKeys();
    SWITCH_TRACE_EVENT("Core", "System::Init.AESInitKeys", "leave");
    SWITCH_TRACE_EVENT("Core", "System::Init.ServiceInit", "enter");
    Service::Init(*this, loading_title_id, lle_modules, !app_loader->DoingInitialSetup());
    SWITCH_TRACE_EVENT("Core", "System::Init.ServiceInit", "leave");
#ifdef ENABLE_GDBSTUB
    GDBStub::DeferStart();
#endif

    if (!registered_image_interface) {
        SWITCH_TRACE_EVENT("Core", "System::Init.ImageInterface", "enter");
        registered_image_interface = std::make_shared<Frontend::ImageInterface>();
        SWITCH_TRACE_EVENT("Core", "System::Init.ImageInterface", "leave");
    }

    SWITCH_TRACE_EVENT("Core", "System::Init.CustomTexManager", "enter");
    custom_tex_manager = std::make_unique<VideoCore::CustomTexManager>(*this);
    SWITCH_TRACE_EVENT("Core", "System::Init.CustomTexManager", "leave");

    SWITCH_TRACE_EVENT("Core", "System::Init.GetGSP", "enter");
    auto gsp = service_manager->GetService<Service::GSP::GSP_GPU>("gsp::Gpu");
    SWITCH_TRACE_EVENTF("Core", "System::Init.GetGSP", "leave", "found=%s",
                        gsp ? "true" : "false");
    SWITCH_TRACE_EVENT("Core", "System::Init.GPU", "enter");
    try {
        gpu = std::make_unique<VideoCore::GPU>(*this, emu_window, secondary_window);
    } catch (const std::exception& e) {
        SWITCH_TRACE_EVENTF("Core", "System::Init.GPU", "failed", "error=%s", e.what());
        LOG_ERROR(Render, "Deko3D renderer initialization failed: {}", e.what());
        return ResultStatus::ErrorRendererInit;
    }
    if (!gpu) {
        SWITCH_TRACE_EVENT("Core", "System::Init.GPU", "failed_null_gpu");
        LOG_ERROR(Render, "Deko3D renderer initialization failed: null GPU");
        return ResultStatus::ErrorRendererInit;
    }
    SWITCH_TRACE_EVENT("Core", "System::Init.GPU", "leave");
    SWITCH_TRACE_EVENT("Core", "System::Init.GPUInterruptHandler", "enter");
    gpu->SetInterruptHandler([gsp](Service::GSP::InterruptId interrupt_id, u64 wait_delay_ns) {
        gsp->SignalInterrupt(interrupt_id, wait_delay_ns);
    });
    SWITCH_TRACE_EVENT("Core", "System::Init.GPUInterruptHandler", "leave");

    SWITCH_TRACE_EVENT("Core", "System::Init.PLG_LDR", "enter");
    auto plg_ldr = Service::PLGLDR::GetService(*this);
    if (plg_ldr) {
        plg_ldr->SetEnabled(Settings::values.plugin_loader_enabled.GetValue());
        plg_ldr->SetAllowGameChangeState(Settings::values.allow_plugin_loader.GetValue());
    }
    SWITCH_TRACE_EVENTF("Core", "System::Init.PLG_LDR", "leave", "found=%s",
                        plg_ldr ? "true" : "false");

    SWITCH_TRACE_EVENT("Core", "System::Init.SetInfoLEDColor", "enter");
    SetInfoLEDColor({});
    SWITCH_TRACE_EVENT("Core", "System::Init.SetInfoLEDColor", "leave");

    LOG_DEBUG(Core, "Initialized OK");

    is_powered_on = true;

    SWITCH_TRACE_EVENT("Core", "System::Init", "leave");
    return ResultStatus::Success;
}

VideoCore::GPU& System::GPU() {
    return *gpu;
}

Service::SM::ServiceManager& System::ServiceManager() {
    return *service_manager;
}

const Service::SM::ServiceManager& System::ServiceManager() const {
    return *service_manager;
}

Service::FS::ArchiveManager& System::ArchiveManager() {
    return *archive_manager;
}

const Service::FS::ArchiveManager& System::ArchiveManager() const {
    return *archive_manager;
}

Kernel::KernelSystem& System::Kernel() {
    return *kernel;
}

const Kernel::KernelSystem& System::Kernel() const {
    return *kernel;
}

bool System::KernelRunning() {
    return kernel != nullptr;
}

Timing& System::CoreTiming() {
    return *timing;
}

const Timing& System::CoreTiming() const {
    return *timing;
}

Memory::MemorySystem& System::Memory() {
    return *memory;
}

const Memory::MemorySystem& System::Memory() const {
    return *memory;
}

Cheats::CheatEngine& System::CheatEngine() {
    return cheat_engine;
}

const Cheats::CheatEngine& System::CheatEngine() const {
    return cheat_engine;
}

void System::RegisterVideoDumper(std::shared_ptr<VideoDumper::Backend> dumper) {
    video_dumper = std::move(dumper);
}

VideoCore::CustomTexManager& System::CustomTexManager() {
    return *custom_tex_manager;
}

const VideoCore::CustomTexManager& System::CustomTexManager() const {
    return *custom_tex_manager;
}

Core::Movie& System::Movie() {
    return movie;
}

const Core::Movie& System::Movie() const {
    return movie;
}

void System::RegisterMiiSelector(std::shared_ptr<Frontend::MiiSelector> mii_selector) {
    registered_mii_selector = std::move(mii_selector);
}

void System::RegisterSoftwareKeyboard(std::shared_ptr<Frontend::SoftwareKeyboard> swkbd) {
    registered_swkbd = std::move(swkbd);
}

void System::RegisterImageInterface(std::shared_ptr<Frontend::ImageInterface> image_interface) {
    registered_image_interface = std::move(image_interface);
}

void System::Shutdown(bool is_deserializing) {

    // Shutdown emulation session
    is_powered_on = false;

    gpu.reset();
    if (!is_deserializing) {
        lle_modules.clear();
#ifdef ENABLE_GDBSTUB
        GDBStub::Shutdown();
#endif
        perf_stats.reset();
        app_loader.reset();
    }
    custom_tex_manager.reset();
#ifdef ENABLE_SCRIPTING
    rpc_server.reset();
#endif
    archive_manager.reset();
    service_manager.reset();
    dsp_core.reset();
    kernel.reset();
    cpu_cores.clear();
    exclusive_monitor.reset();
    timing.reset();

    if (video_dumper && video_dumper->IsDumping()) {
        video_dumper->StopDumping();
    }

    if (auto room_member = Network::GetRoomMember().lock()) {
        Network::GameInfo game_info{};
        room_member->SendGameInfo(game_info);
    }

    memory.reset();

    SetInfoLEDColor({});

    LOG_DEBUG(Core, "Shutdown OK");
}

void System::Reset() {
    // This is NOT a proper reset, but a temporary workaround by shutting down the system and
    // reloading.
    // TODO: Properly implement the reset

    // Save the APT deliver arg and plugin loader context across resets.
    // This is needed as we don't currently support proper app jumping.
    if (auto apt = Service::APT::GetModule(*this)) {
        restore_deliver_arg = apt->GetAppletManager()->ReceiveDeliverArg();
        restore_sys_menu_arg = apt->GetAppletManager()->GetSysMenuArg();
        restore_wireless_reboot_info = apt->GetWirelessRebootInfoBuffer();
    }
    if (auto plg_ldr = Service::PLGLDR::GetService(*this)) {
        restore_plugin_context = plg_ldr->GetPluginLoaderContext();
    }

    restore_ipc_recorder = std::move(kernel->BackupIPCRecorder());

    Shutdown();

    if (!m_chainloadpath.empty()) {
        m_filepath = m_chainloadpath;
        m_chainloadpath.clear();
    }

    // Reload the system with the same setting
    [[maybe_unused]] const System::ResultStatus result =
        Load(*m_emu_window, m_filepath, m_secondary_window);
}

void System::ApplySettings() {
#ifdef ENABLE_GDBSTUB
    GDBStub::SetServerPort(Settings::values.gdbstub_port.GetValue());
    GDBStub::ToggleServer(Settings::values.use_gdbstub.GetValue());
#endif

    if (gpu) {
#ifndef ANDROID
        gpu->Renderer().UpdateCurrentFramebufferLayout();
#endif
        auto& settings = gpu->Renderer().Settings();
        settings.bg_color_update_requested = true;
        settings.shader_update_requested = true;
    }

    if (IsPoweredOn()) {
        CoreTiming().UpdateClockSpeed(Settings::values.cpu_clock_percentage.GetValue());
        dsp_core->SetSink(Settings::values.output_type.GetValue(),
                          Settings::values.output_device.GetValue());
        dsp_core->EnableStretching(Settings::values.enable_audio_stretching.GetValue());

        auto hid = Service::HID::GetModule(*this);
        if (hid) {
            hid->ReloadInputDevices();
        }

        auto apt = Service::APT::GetModule(*this);
        if (apt) {
            apt->GetAppletManager()->ReloadInputDevices();
        }

        auto ir_user = service_manager->GetService<Service::IR::IR_USER>("ir:USER");
        if (ir_user)
            ir_user->ReloadInputDevices();
        auto ir_rst = service_manager->GetService<Service::IR::IR_RST>("ir:rst");
        if (ir_rst)
            ir_rst->ReloadInputDevices();

        auto cam = Service::CAM::GetModule(*this);
        if (cam) {
            cam->ReloadCameraDevices();
        }

        Service::MIC::ReloadMic(*this);
    }

    auto plg_ldr = Service::PLGLDR::GetService(*this);
    if (plg_ldr) {
        plg_ldr->SetEnabled(Settings::values.plugin_loader_enabled.GetValue());
        plg_ldr->SetAllowGameChangeState(Settings::values.allow_plugin_loader.GetValue());
    }
}

void System::RegisterAppLoaderEarly(std::unique_ptr<Loader::AppLoader>& loader) {
    early_app_loader = std::move(loader);
}

void System::InsertCartridge(const std::string& path) {
    FileSys::NCCHContainer cartridge_container(path);
    if (cartridge_container.LoadHeader() == Loader::ResultStatus::Success &&
        cartridge_container.IsNCSD()) {
        inserted_cartridge = path;
    }
}

void System::EjectCartridge() {
    inserted_cartridge.clear();
}

bool System::IsInitialSetup() {
    return app_loader && app_loader->DoingInitialSetup();
}

void System::DebugUnscheduleAllThreadsFromFrontend(bool unschedule) {
    if (!is_powered_on)
        return;

    for (auto proc : kernel->GetProcessList()) {
        if (unschedule) {
            proc->SetUnscheduleMode(Kernel::UnscheduleMode::FRONTEND);
        } else {
            proc->ClearUnscheduleMode(Kernel::UnscheduleMode::FRONTEND);
        }
    }
}

template <class Archive>
void System::serialize(Archive& ar, const unsigned int file_version) {

    if (Archive::is_loading::value) {
        save_state_status = SaveStateStatus::LOADING;
    } else {
        save_state_status = SaveStateStatus::SAVING;
    }

    u32 num_cores;
    if (Archive::is_saving::value) {
        num_cores = this->GetNumCores();
    }
    ar & num_cores;

    // TODO(PabloMK7): Figure out why this is the case
    if (!lle_modules.empty()) {
        throw std::runtime_error("Savestates are not supported with LLE modules enabled");
    }

    ar & lle_modules;
    Kernel::MemoryMode mem_mode{};
    if (!Archive::is_loading::value) {
        mem_mode = kernel->GetMemoryMode();
    }
    ar & mem_mode;

    if (Archive::is_loading::value) {
        // When loading, we want to make sure any lingering state gets cleared out before we begin.
        // Shutdown, but persist a few things between loads...
        Shutdown(true);

        [[maybe_unused]] const System::ResultStatus result =
            Init(*m_emu_window, m_secondary_window, mem_mode, num_cores);
    }

    // Flush on save, don't flush on load
    const bool should_flush = !Archive::is_loading::value;
    gpu->ClearAll(should_flush);
    ar&* timing.get();
    for (u32 i = 0; i < num_cores; i++) {
        ar&* cpu_cores[i].get();
    }
    ar&* service_manager.get();
    ar&* archive_manager.get();

    // NOTE: DSP doesn't like being destroyed and recreated. So instead we do an inline
    // serialization; this means that the DSP Settings need to match for loading to work.
    auto dsp_hle = dynamic_cast<AudioCore::DspHle*>(dsp_core.get());
    if (dsp_hle) {
        ar&* dsp_hle;
    } else {
        throw std::runtime_error("LLE audio not supported for save states");
    }

    ar&* memory.get();
    ar&* kernel.get();
    ar&* gpu.get();
    ar & movie;

    // This needs to be set from somewhere - might as well be here!
    if (Archive::is_loading::value) {
        u32 cheats_pid;
        ar & cheats_pid;
        timing->UnlockEventQueue();
        cheat_engine.Connect(cheats_pid);

        if (Settings::values.custom_textures) {
            custom_tex_manager->FindCustomTextures();
        }

        // Re-register gpu callback, because gsp service changed after service_manager got
        // serialized
        auto gsp = service_manager->GetService<Service::GSP::GSP_GPU>("gsp::Gpu");
        gpu->SetInterruptHandler([gsp](Service::GSP::InterruptId interrupt_id, u64 wait_delay_ns) {
            gsp->SignalInterrupt(interrupt_id, wait_delay_ns);
        });

        // Apply per program settings and switch the shader cache to the title running when the
        // savestate was created.
        // TODO(PabloMK7): Find better way to obtain the program ID.
        const u32 thread_id = gsp->GetActiveClientThreadId();
        if (thread_id != std::numeric_limits<u32>::max()) {
            const auto thread = kernel->GetThreadByID(thread_id);
            if (thread) {
                const std::shared_ptr<Kernel::Process> process = thread->owner_process.lock();
                if (process) {
                    gpu->ApplyPerProgramSettings(process->codeset->program_id);
                    gpu->Renderer().Rasterizer()->SwitchDiskResources(process->codeset->program_id);
                }
            }
        }
    } else {
        u32 cheats_pid = cheat_engine.GetConnectedPID();
        ar & cheats_pid;
    }

    save_state_status = SaveStateStatus::NONE;
}

SERIALIZE_IMPL(System)

} // namespace Core
