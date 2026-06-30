// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <cstdio>
#include <cstdint>

#include <switch.h>

// These symbols are discovered by the libnx startup code. Keep C linkage and
// avoid C++ allocations/locks in the handler because the process is already in
// an exceptional state.
extern "C" {

alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump* context) {
    FILE* file = std::fopen("sdmc:/switch/azahar/logs/exception_dump.txt", "w");
    if (file == nullptr) {
        return;
    }

    std::fprintf(file, "Azahar NX exception dump\n");
    if (context == nullptr) {
        std::fprintf(file, "context=null\n");
        std::fflush(file);
        std::fclose(file);
        return;
    }

    std::fprintf(file, "error_desc=0x%08x\n", context->error_desc);
    for (int index = 0; index < 29; ++index) {
        std::fprintf(file, "x%d=0x%016llx\n", index,
                     static_cast<unsigned long long>(context->cpu_gprs[index].x));
    }

    std::fprintf(file, "fp=0x%016llx\n",
                 static_cast<unsigned long long>(context->fp.x));
    std::fprintf(file, "lr=0x%016llx\n",
                 static_cast<unsigned long long>(context->lr.x));
    std::fprintf(file, "sp=0x%016llx\n",
                 static_cast<unsigned long long>(context->sp.x));
    std::fprintf(file, "pc=0x%016llx\n",
                 static_cast<unsigned long long>(context->pc.x));
    std::fprintf(file, "pstate=0x%08x\n", context->pstate);
    std::fprintf(file, "afsr0=0x%08x\n", context->afsr0);
    std::fprintf(file, "afsr1=0x%08x\n", context->afsr1);
    std::fprintf(file, "esr=0x%08x\n", context->esr);
    std::fprintf(file, "far=0x%016llx\n",
                 static_cast<unsigned long long>(context->far.x));

    std::fflush(file);
    std::fclose(file);
}

} // extern "C"
