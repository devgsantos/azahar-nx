// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_debug_log.h"
#include "switch_nxlink.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <sys/types.h>

namespace Azahar::Switch::DebugLog {
namespace {

constexpr const char* LogDir = "sdmc:/switch/azahar/logs";
constexpr const char* LogPath = "sdmc:/switch/azahar/logs/azahar-switch-early.log";

void EnsureLogDir() {
    // Best effort only. Do not allow logging failure to crash the frontend.
    mkdir("sdmc:/switch", 0777);
    mkdir("sdmc:/switch/azahar", 0777);
    mkdir(LogDir, 0777);
}

void AppendRaw(const char* line) {
    NxLink::WriteLine(line);
    EnsureLogDir();
    FILE* file = std::fopen(LogPath, "a");
    if (!file) {
        return;
    }
    std::fputs(line ? line : "<null>", file);
    std::fputc('\n', file);
    std::fflush(file);
    std::fclose(file);
}

} // namespace

void Initialize() {
    EnsureLogDir();
    FILE* file = std::fopen(LogPath, "a");
    if (!file) {
        return;
    }
    std::fputs("\n==== Azahar Switch early log session start ====\n", file);
    std::fflush(file);
    std::fclose(file);
}

void WriteLine(const char* message) {
    AppendRaw(message);
}

void WriteLine(const std::string& message) {
    AppendRaw(message.c_str());
}

void WriteFormat(const char* fmt, ...) {
    char buffer[1024]{};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    AppendRaw(buffer);
}

} // namespace Azahar::Switch::DebugLog
