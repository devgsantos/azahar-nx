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

void AppendTraceLine(const char* module, const char* scope, const char* event,
                     const char* detail) {
#ifdef AZAHAR_SWITCH
    EnsureLogDir();
    FILE* file = std::fopen(LogPath, "a");
    if (!file) {
        return;
    }

    const auto id = ++sequence;
    std::fprintf(file, "TRACE #%06llu module=%s scope=%s event=%s", id,
                 module ? module : "<null>", scope ? scope : "<null>", event ? event : "<null>");
    if (detail && std::strlen(detail) > 0) {
        std::fprintf(file, " detail=%s", detail);
    }
    std::fputc('\n', file);
    std::fflush(file);
    std::fclose(file);
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
