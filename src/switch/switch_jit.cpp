// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_jit.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

// IMPORTANT:
// Keep this translation unit isolated from Azahar common headers.
// libnx defines a global u128 typedef, while Azahar defines its own global u128
// alias. Including both header families in one translation unit causes a hard
// type-name collision.
#include <switch.h>

namespace Azahar::Switch {

namespace {

constexpr std::size_t JitTestBufferSize = 0x1000;
constexpr std::uint64_t JitExpectedResult = 42;
constexpr const char* JitLogPath =
    "sdmc:/switch/azahar/logs/azahar-switch-early.log";

void AppendJitLog(const char* format, ...) {
    FILE* file = std::fopen(JitLogPath, "a");
    if (file == nullptr) {
        return;
    }

    std::fputs("[Switch.JIT] ", file);

    va_list arguments;
    va_start(arguments, format);
    std::vfprintf(file, format, arguments);
    va_end(arguments);

    std::fputc('\n', file);
    std::fflush(file);
    std::fclose(file);
}

bool LogResultFailure(const char* operation, Result result) {
    AppendJitLog("Switch JIT self-test %s failed result=0x%08x", operation,
                 static_cast<unsigned>(result));
    return false;
}

} // namespace

bool RunJitSelfTest() {
    AppendJitLog("Switch JIT self-test enter");

    Jit jit{};
    Result result = jitCreate(&jit, JitTestBufferSize);
    if (R_FAILED(result)) {
        return LogResultFailure("jitCreate", result);
    }

    bool passed = false;
    do {
        auto* const writable = static_cast<std::uint8_t*>(jitGetRwAddr(&jit));
        auto* const executable = static_cast<std::uint8_t*>(jitGetRxAddr(&jit));

        AppendJitLog("Switch JIT buffer created type=%d size=%zu rw=%p rx=%p",
                     static_cast<int>(jit.type), JitTestBufferSize,
                     static_cast<void*>(writable), static_cast<void*>(executable));

        if (writable == nullptr || executable == nullptr) {
            AppendJitLog("Switch JIT self-test failed: null RW/RX alias");
            break;
        }

        result = jitTransitionToWritable(&jit);
        if (R_FAILED(result)) {
            LogResultFailure("jitTransitionToWritable", result);
            break;
        }
        AppendJitLog("Switch JIT transitioned to writable");

        // AArch64: movz x0, #42; ret
        constexpr std::uint32_t test_code[] = {
            0xD2800540,
            0xD65F03C0,
        };

        std::memcpy(writable, test_code, sizeof(test_code));
        armDCacheFlush(writable, sizeof(test_code));
        AppendJitLog("Switch JIT test code emitted and data cache flushed");

        result = jitTransitionToExecutable(&jit);
        if (R_FAILED(result)) {
            LogResultFailure("jitTransitionToExecutable", result);
            break;
        }

        armICacheInvalidate(executable, sizeof(test_code));
        AppendJitLog(
            "Switch JIT transitioned to executable and instruction cache invalidated");

        using TestFunction = std::uint64_t (*)();
        const auto function = reinterpret_cast<TestFunction>(executable);
        const std::uint64_t returned = function();
        AppendJitLog("Switch JIT test function returned %llu",
                     static_cast<unsigned long long>(returned));

        if (returned != JitExpectedResult) {
            AppendJitLog("Switch JIT self-test returned %llu, expected %llu",
                         static_cast<unsigned long long>(returned),
                         static_cast<unsigned long long>(JitExpectedResult));
            break;
        }

        passed = true;
    } while (false);

    const Result close_result = jitClose(&jit);
    if (R_FAILED(close_result)) {
        LogResultFailure("jitClose", close_result);
        passed = false;
    }

    AppendJitLog("Switch JIT self-test leave result=%s",
                 passed ? "passed" : "failed");
    return passed;
}

} // namespace Azahar::Switch
