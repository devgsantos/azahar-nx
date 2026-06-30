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
#if defined(AZAHAR_SWITCH) || defined(__SWITCH__)
    EnsureLogDir();
    char line[1536]{};
    FILE* file = std::fopen(LogPath, "a");

    const auto id = ++sequence;
    std::snprintf(line, sizeof(line), "TRACE #%06llu module=%s scope=%s event=%s", id,
                  module ? module : "<null>", scope ? scope : "<null>", event ? event : "<null>");
    if (detail && std::strlen(detail) > 0) {
        const std::size_t used = std::strlen(line);
        std::snprintf(line + used, sizeof(line) - used, " detail=%s", detail);
    }

    std::fprintf(stderr, "%s\n", line);
    std::fflush(stderr);

    if (!file) {
        return;
    }
    std::fprintf(file, "%s\n", line);
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
