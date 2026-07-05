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
constexpr unsigned long long FlushInterval = 64;
constexpr std::size_t FileBufferSize = 64 * 1024;
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
            std::setvbuf(opened, nullptr, _IOFBF, FileBufferSize);
        }
        return opened;
    }();
    return file;
#else
    return nullptr;
#endif
}

bool ShouldFlush(const char* module, const char* event, unsigned long long id) {
    if ((id % FlushInterval) == 0) {
        return true;
    }

    // Preserve the last useful boundary before entering generated code and
    // callback activity while avoiding a synchronous SD-card flush per trace.
    if (event != nullptr && std::strcmp(event, "before jit->Run or jit->Step") == 0) {
        return true;
    }
    if (module != nullptr && std::strcmp(module, "Dynarmic.Callback") == 0) {
        return true;
    }
    return false;
}

void AppendTraceLine(const char* module, const char* scope, const char* event,
                     const char* detail) {
#if defined(AZAHAR_SWITCH) || defined(__SWITCH__)
    char line[1536]{};
    const auto id = ++sequence;
    std::snprintf(line, sizeof(line), "TRACE #%06llu module=%s scope=%s event=%s", id,
                  module ? module : "<null>", scope ? scope : "<null>", event ? event : "<null>");
    if (detail && std::strlen(detail) > 0) {
        const std::size_t used = std::strlen(line);
        std::snprintf(line + used, sizeof(line) - used, " detail=%s", detail);
    }

    // stderr is kept for nxlink/live diagnostics. Avoid an explicit fflush on
    // every line; stderr is normally unbuffered and the file sink below keeps
    // bounded crash-loss through periodic and boundary flushes.
    std::fprintf(stderr, "%s\n", line);

    FILE* file = GetTraceFile();
    if (file == nullptr) {
        return;
    }

    std::fprintf(file, "%s\n", line);
    if (ShouldFlush(module, event, id)) {
        std::fflush(file);
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
