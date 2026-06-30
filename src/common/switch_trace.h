// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

namespace Common::SwitchTrace {

void Write(const char* module, const char* scope, const char* event);
void WriteFormat(const char* module, const char* scope, const char* event, const char* fmt, ...);

class ScopedTrace {
public:
    ScopedTrace(const char* module, const char* scope);
    ScopedTrace(const char* module, const char* scope, const char* detail);
    ~ScopedTrace();

    ScopedTrace(const ScopedTrace&) = delete;
    ScopedTrace& operator=(const ScopedTrace&) = delete;

private:
    const char* module;
    const char* scope;
};

} // namespace Common::SwitchTrace

#define SWITCH_TRACE_CONCAT_INNER(a, b) a##b
#define SWITCH_TRACE_CONCAT(a, b) SWITCH_TRACE_CONCAT_INNER(a, b)
#define SWITCH_TRACE_SCOPE(module, scope)                                                          \
    ::Common::SwitchTrace::ScopedTrace SWITCH_TRACE_CONCAT(switch_trace_scope_, __LINE__)(module,  \
                                                                                          scope)
#define SWITCH_TRACE_SCOPE_DETAIL(module, scope, detail)                                           \
    ::Common::SwitchTrace::ScopedTrace SWITCH_TRACE_CONCAT(switch_trace_scope_, __LINE__)(          \
        module, scope, detail)
#define SWITCH_TRACE_EVENT(module, scope, event)                                                    \
    ::Common::SwitchTrace::Write(module, scope, event)
#define SWITCH_TRACE_EVENTF(module, scope, event, fmt, ...)                                         \
    ::Common::SwitchTrace::WriteFormat(module, scope, event, fmt, ##__VA_ARGS__)
