// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <cstdio>
#include <cstdint>

#include <switch.h>

// These symbols are discovered by the libnx startup code. Keep C linkage and
// avoid C++ allocations/locks in the handler because the process is already in
// an exceptional state.
extern "C" {

struct AzaharDynarmicJitBreadcrumb {
    std::uint32_t phase;
    std::uint32_t svc;
    std::uintptr_t sp;
    std::uintptr_t lr;
    std::uintptr_t x16;
    std::uintptr_t x17;
};

const char* azahar_switch_dynarmic_jit_describe_address(
    std::uintptr_t address, char* buffer, std::size_t buffer_size);
void azahar_switch_dynarmic_jit_get_breadcrumb(
    AzaharDynarmicJitBreadcrumb* out);

alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

static void DumpAddress(FILE* file, const char* name, std::uintptr_t address) {
    char description[192]{};
    azahar_switch_dynarmic_jit_describe_address(address, description,
                                                sizeof(description));
    std::fprintf(file, "%s_class=%s\n", name, description);
}

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
    DumpAddress(file, "pc", static_cast<std::uintptr_t>(context->pc.x));
    DumpAddress(file, "lr", static_cast<std::uintptr_t>(context->lr.x));
    DumpAddress(file, "far", static_cast<std::uintptr_t>(context->far.x));
    DumpAddress(file, "x16",
                static_cast<std::uintptr_t>(context->cpu_gprs[16].x));
    DumpAddress(file, "x17",
                static_cast<std::uintptr_t>(context->cpu_gprs[17].x));

    AzaharDynarmicJitBreadcrumb breadcrumb{};
    azahar_switch_dynarmic_jit_get_breadcrumb(&breadcrumb);
    std::fprintf(file, "jit_breadcrumb_phase=%u\n", breadcrumb.phase);
    std::fprintf(file, "jit_breadcrumb_svc=0x%08x\n", breadcrumb.svc);
    std::fprintf(file, "jit_breadcrumb_sp=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.sp));
    std::fprintf(file, "jit_breadcrumb_lr=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.lr));
    std::fprintf(file, "jit_breadcrumb_x16=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.x16));
    std::fprintf(file, "jit_breadcrumb_x17=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.x17));
    DumpAddress(file, "jit_breadcrumb_lr",
                static_cast<std::uintptr_t>(breadcrumb.lr));
    DumpAddress(file, "jit_breadcrumb_x16",
                static_cast<std::uintptr_t>(breadcrumb.x16));
    DumpAddress(file, "jit_breadcrumb_x17",
                static_cast<std::uintptr_t>(breadcrumb.x17));

    std::fflush(file);
    std::fclose(file);
}

} // extern "C"
