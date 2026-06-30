// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <string>

namespace Azahar::Switch::DebugLog {

void Initialize();
void WriteLine(const char* message);
void WriteLine(const std::string& message);
void WriteFormat(const char* fmt, ...);

} // namespace Azahar::Switch::DebugLog

#define SWITCH_EARLY_LOG(message) ::Azahar::Switch::DebugLog::WriteLine(message)
#define SWITCH_EARLY_LOGF(fmt, ...) ::Azahar::Switch::DebugLog::WriteFormat(fmt, ##__VA_ARGS__)
