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

#if defined(AZAHAR_SWITCH_EARLY_LOG_ENABLED)
#define SWITCH_EARLY_LOG(message) ::Azahar::Switch::DebugLog::WriteLine(message)
#define SWITCH_EARLY_LOGF(fmt, ...) ::Azahar::Switch::DebugLog::WriteFormat(fmt, ##__VA_ARGS__)
#else
#define SWITCH_EARLY_LOG(message) ((void)0)
#define SWITCH_EARLY_LOGF(fmt, ...) ((void)0)
#endif
