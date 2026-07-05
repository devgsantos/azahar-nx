// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "common/switch_trace.h"

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

namespace Common::SwitchTrace {
namespace {

constexpr const char* LogDir = "sdmc:/switch/azahar/logs";
constexpr const char* LogPath = "sdmc:/switch/azahar/logs/azahar-switch-early.log";
std::atomic<unsigned long long> sequence{0};

void EnsureLogDir() {
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/azahar", 0777);
    mkdir(LogDir, 0777);
}

FILE* GetTraceFile() {
#if defined(AZAHAR_SWITCH) || defined(__SWITCH__)
    static FILE* file = []() -> FILE* {
        EnsureLogDir();
        FILE* opened = std::fopen(LogPath, "a");
        if (opened != nullptr) {
            // Keep ordering deterministic with the other early-log writers and
            // preserve the final trace boundary on a crash, while avoiding the
            // former mkdir/fopen/fflush/fclose cycle for every trace event.
            std::setvbuf(opened, nullptr, _IONBF, 0);
        }
        return opened;
    }();
    return file;
#else
    return nullptr;
#endif
}

bool ShouldWriteTrace(const char* module, const char* scope, const char* event) {
    if (module == nullptr || scope == nullptr || event == nullptr) {
        return true;
    }

    // System::RunLoop generated more than 8,000 synchronous SD-card writes
    // before the first frame in the v16 diagnostic run. The in-memory crash
    // breadcrumbs remain active, so these textual enter/leave details are no
    // longer needed on the performance path.
    if (std::strcmp(module, "Core") == 0 &&
        std::strcmp(scope, "System::RunLoop") == 0) {
        return false;
    }

    // Keep only the two useful Dynarmic progress records. Callback traces use
    // their own module and are intentionally preserved for crash diagnosis.
    if (std::strcmp(module, "Core.ARM") == 0 &&
        std::strcmp(scope, "ARM_Dynarmic::Run") == 0) {
        return std::strcmp(event, "cycle budget consumed") == 0 ||
               std::strcmp(event, "after jit->Run or jit->Step") == 0;
    }

    return true;
}

void AppendTraceLine(const char* module, const char* scope, const char* event,
                     const char* detail) {
#if defined(AZAHAR_SWITCH) || defined(__SWITCH__)
    if (!ShouldWriteTrace(module, scope, event)) {
        return;
    }

    char line[1536]{};
    const auto id = ++sequence;
    std::snprintf(line, sizeof(line), "TRACE #%06llu module=%s scope=%s event=%s", id,
                  module ? module : "<null>", scope ? scope : "<null>", event ? event : "<null>");
    if (detail && std::strlen(detail) > 0) {
        const std::size_t used = std::strlen(line);
        std::snprintf(line + used, sizeof(line) - used, " detail=%s", detail);
    }

    // stderr is retained for nxlink/live diagnostics. It is normally
    // unbuffered, so an explicit fflush per trace is unnecessary.
    std::fprintf(stderr, "%s\n", line);

    FILE* file = GetTraceFile();
    if (file != nullptr) {
        std::fprintf(file, "%s\n", line);
    }
#else
    (void)module;
    (void)scope;
    (void)event;
    (void)detail;
#endif
}

} // namespace

void Write(const char* module, const char* scope, const char* event) {
    AppendTraceLine(module, scope, event, nullptr);
}

void WriteFormat(const char* module, const char* scope, const char* event, const char* fmt, ...) {
    char detail[1024]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(detail, sizeof(detail), fmt, args);
    va_end(args);
    AppendTraceLine(module, scope, event, detail);
}

ScopedTrace::ScopedTrace(const char* module_, const char* scope_)
    : module(module_), scope(scope_) {
    Write(module, scope, "enter");
}

ScopedTrace::ScopedTrace(const char* module_, const char* scope_, const char* detail)
    : module(module_), scope(scope_) {
    WriteFormat(module, scope, "enter", "%s", detail ? detail : "");
}

ScopedTrace::~ScopedTrace() {
    Write(module, scope, "leave");
}

} // namespace Common::SwitchTrace
