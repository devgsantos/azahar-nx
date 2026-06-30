// SPDX-FileCopyrightText: Copyright (c) 2022 merryhime <https://mary.rs>
// SPDX-License-Identifier: MIT

#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <new>

#if defined(_WIN32)
#    define NOMINMAX
#    include <windows.h>
#elif defined(__APPLE__)
#    include <TargetConditionals.h>
#    include <libkern/OSCacheControl.h>
#    include <pthread.h>
#    include <sys/mman.h>
#    include <unistd.h>
#elif defined(__SWITCH__)
typedef std::uint32_t Result;
typedef std::uint32_t Handle;
typedef struct VirtmemReservation VirtmemReservation;
typedef enum {
    JitType_SetProcessMemoryPermission,
    JitType_CodeMemory,
} JitType;
typedef struct {
    JitType type;
    std::size_t size;
    void* src_addr;
    void* rx_addr;
    void* rw_addr;
    bool is_executable;
    union {
        Handle handle;
        VirtmemReservation* rv;
    };
} Jit;
extern "C" Result jitCreate(Jit* j, std::size_t size);
extern "C" Result jitTransitionToWritable(Jit* j);
extern "C" Result jitTransitionToExecutable(Jit* j);
extern "C" Result jitClose(Jit* j);
extern "C" void armDCacheFlush(void* addr, std::size_t size);
#else
#    include <sys/mman.h>
#endif

namespace oaknut {

class CodeBlock {
public:
    explicit CodeBlock(std::size_t size)
        : m_size(size)
    {
#if defined(_WIN32)
        m_memory = (std::uint32_t*)VirtualAlloc(nullptr, size, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
#elif defined(__SWITCH__)
        RunSwitchJitSelfTest();
        if (jitCreate(&m_jit, size) != 0)
            throw std::bad_alloc{};
        if (jitTransitionToWritable(&m_jit) != 0)
            throw std::bad_alloc{};
        m_memory = (std::uint32_t*)m_jit.rw_addr;
        m_executable_memory = (std::uint32_t*)m_jit.rx_addr;
#elif defined(__APPLE__)
#    if TARGET_OS_IPHONE
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
#    else
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);
#    endif
#elif defined(__NetBSD__)
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_MPROTECT(PROT_READ | PROT_WRITE | PROT_EXEC), MAP_ANON | MAP_PRIVATE, -1, 0);
#elif defined(__OpenBSD__)
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_READ | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
#else
        m_memory = (std::uint32_t*)mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
#endif

        if (m_memory == nullptr)
            throw std::bad_alloc{};
#if defined(__SWITCH__)
        if (m_executable_memory == nullptr)
            throw std::bad_alloc{};
#endif
    }

    ~CodeBlock()
    {
        if (m_memory == nullptr)
            return;

#if defined(_WIN32)
        VirtualFree((void*)m_memory, 0, MEM_RELEASE);
#elif defined(__SWITCH__)
        jitClose(&m_jit);
#else
        munmap(m_memory, m_size);
#endif
    }

    CodeBlock(const CodeBlock&) = delete;
    CodeBlock& operator=(const CodeBlock&) = delete;
    CodeBlock(CodeBlock&&) = delete;
    CodeBlock& operator=(CodeBlock&&) = delete;

    std::uint32_t* ptr() const
    {
        return m_memory;
    }

    std::uint32_t* xptr() const
    {
#if defined(__SWITCH__)
        return m_executable_memory;
#else
        return m_memory;
#endif
    }

    void protect()
    {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
        pthread_jit_write_protect_np(1);
#elif defined(__SWITCH__)
        jitTransitionToExecutable(&m_jit);
#elif defined(__APPLE__) || defined(__NetBSD__) || defined(__OpenBSD__)
        mprotect(m_memory, m_size, PROT_READ | PROT_EXEC);
#endif
    }

    void unprotect()
    {
#if defined(__APPLE__) && !TARGET_OS_IPHONE
        pthread_jit_write_protect_np(0);
#elif defined(__SWITCH__)
        jitTransitionToWritable(&m_jit);
#elif defined(__APPLE__) || defined(__NetBSD__) || defined(__OpenBSD__)
        mprotect(m_memory, m_size, PROT_READ | PROT_WRITE);
#endif
    }

    void invalidate(std::uint32_t* mem, std::size_t size)
    {
#if defined(__APPLE__)
        sys_icache_invalidate(mem, size);
#elif defined(_WIN32)
        FlushInstructionCache(GetCurrentProcess(), mem, size);
#elif defined(__SWITCH__)
        const auto offset =
            reinterpret_cast<std::uintptr_t>(mem) - reinterpret_cast<std::uintptr_t>(xptr());
        auto* const rw_mem = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(m_memory) + offset);
        auto* const rx_mem = reinterpret_cast<char*>(reinterpret_cast<std::uintptr_t>(xptr()) + offset);
        armDCacheFlush(rw_mem, size);
        __builtin___clear_cache(rx_mem, rx_mem + size);
#else
        static std::size_t icache_line_size = 0x10000, dcache_line_size = 0x10000;

        std::uint64_t ctr;
        __asm__ volatile("mrs %0, ctr_el0"
                         : "=r"(ctr));

        const std::size_t isize = icache_line_size = std::min<std::size_t>(icache_line_size, 4 << ((ctr >> 0) & 0xf));
        const std::size_t dsize = dcache_line_size = std::min<std::size_t>(dcache_line_size, 4 << ((ctr >> 16) & 0xf));

        const std::uintptr_t end = (std::uintptr_t)mem + size;

        for (std::uintptr_t addr = ((std::uintptr_t)mem) & ~(dsize - 1); addr < end; addr += dsize) {
            __asm__ volatile("dc cvau, %0"
                             :
                             : "r"(addr)
                             : "memory");
        }
        __asm__ volatile("dsb ish\n"
                         :
                         :
                         : "memory");

        for (std::uintptr_t addr = ((std::uintptr_t)mem) & ~(isize - 1); addr < end; addr += isize) {
            __asm__ volatile("ic ivau, %0"
                             :
                             : "r"(addr)
                             : "memory");
        }
        __asm__ volatile("dsb ish\nisb\n"
                         :
                         :
                         : "memory");
#endif
    }

    void invalidate_all()
    {
        invalidate(xptr(), m_size);
    }

protected:
#if defined(__SWITCH__)
    static void RunSwitchJitSelfTest()
    {
        static bool tested = false;
        if (tested)
            return;

        Jit test_jit{};
        if (jitCreate(&test_jit, 0x1000) != 0)
            throw std::bad_alloc{};

        auto close_jit = [&] {
            jitClose(&test_jit);
        };

        if (jitTransitionToWritable(&test_jit) != 0) {
            close_jit();
            throw std::bad_alloc{};
        }

        auto* const rw = static_cast<std::uint32_t*>(test_jit.rw_addr);
        auto* const rx = static_cast<std::uint32_t*>(test_jit.rx_addr);
        if (rw == nullptr || rx == nullptr) {
            close_jit();
            throw std::bad_alloc{};
        }

        rw[0] = 0x52801540; // mov w0, #0xaa
        rw[1] = 0xD65F03C0; // ret
        armDCacheFlush(rw, 2 * sizeof(std::uint32_t));
        __builtin___clear_cache(reinterpret_cast<char*>(rx),
                                reinterpret_cast<char*>(rx + 2));

        if (jitTransitionToExecutable(&test_jit) != 0) {
            close_jit();
            throw std::bad_alloc{};
        }

        using TestFunc = int (*)();
        const int result = reinterpret_cast<TestFunc>(rx)();
        if (jitTransitionToWritable(&test_jit) != 0) {
            close_jit();
            throw std::bad_alloc{};
        }
        close_jit();

        if (result != 0xaa)
            throw std::bad_alloc{};

        tested = true;
    }
#endif

    std::uint32_t* m_memory;
    std::size_t m_size = 0;
#if defined(__SWITCH__)
    std::uint32_t* m_executable_memory = nullptr;
    Jit m_jit{};
#endif
};

}  // namespace oaknut
