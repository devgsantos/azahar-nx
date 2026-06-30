// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include <cstdio>
#include <cstdint>
#include <atomic>

#include <switch.h>

// These symbols are discovered by the libnx startup code. Keep C linkage and
// avoid C++ allocations/locks in the handler because the process is already in
// an exceptional state.
extern "C" {

struct AzaharDynarmicJitBreadcrumb {
    std::atomic<std::uint32_t> phase;
    std::uintptr_t block_entry;
    std::uintptr_t callback_target;
    std::uintptr_t continuation;
    std::uintptr_t dispatcher_target;
    std::uintptr_t host_lr;
    std::uintptr_t host_sp;
    std::uintptr_t host_x16;
    std::uintptr_t host_x17;
    std::uint32_t guest_pc;
    std::uint32_t svc;
};

enum class AzaharJitAddressClass : std::uint32_t {
    Outside,
    RW,
    RX,
};

struct AzaharJitAddressInfo {
    AzaharJitAddressClass address_class;
    const char* owner;
    std::uint32_t id;
    std::size_t offset;
    std::uintptr_t other_alias;
};

void azahar_switch_dynarmic_jit_classify_address(
    std::uintptr_t address, AzaharJitAddressInfo* out);
void azahar_switch_dynarmic_jit_get_breadcrumb(
    AzaharDynarmicJitBreadcrumb* out);

alignas(16) u8 __nx_exception_stack[0x4000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

static constexpr const char* RequiredJitDumpFields =
    "pc_jit_class lr_jit_class far_jit_class x16_jit_class x17_jit_class";

static const char* JitClassName(AzaharJitAddressClass address_class) {
    switch (address_class) {
    case AzaharJitAddressClass::RW:
        return "RW";
    case AzaharJitAddressClass::RX:
        return "RX";
    case AzaharJitAddressClass::Outside:
    default:
        return "Outside";
    }
}

static void DumpJitAddress(FILE* file, const char* name, std::uintptr_t address) {
    AzaharJitAddressInfo info{};
    azahar_switch_dynarmic_jit_classify_address(address, &info);
    std::fprintf(file, "%s_jit_class=%s\n", name, JitClassName(info.address_class));
    std::fprintf(file, "%s_jit_owner=%s\n", name,
                 info.owner != nullptr ? info.owner : "none");
    std::fprintf(file, "%s_jit_id=%u\n", name, info.id);
    std::fprintf(file, "%s_jit_offset=0x%zx\n", name, info.offset);
    std::fprintf(file, "%s_other_alias=0x%016llx\n", name,
                 static_cast<unsigned long long>(info.other_alias));
}

void __libnx_exception_handler(ThreadExceptionDump* context) {
    FILE* file = std::fopen("sdmc:/switch/azahar/logs/exception_dump.txt", "w");
    if (file == nullptr) {
        return;
    }

    std::fprintf(file, "Azahar NX exception dump\n");
    std::fprintf(file, "jit_dump_fields=%s\n", RequiredJitDumpFields);
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
    DumpJitAddress(file, "pc", static_cast<std::uintptr_t>(context->pc.x));
    DumpJitAddress(file, "lr", static_cast<std::uintptr_t>(context->lr.x));
    DumpJitAddress(file, "far", static_cast<std::uintptr_t>(context->far.x));
    DumpJitAddress(file, "x16",
                   static_cast<std::uintptr_t>(context->cpu_gprs[16].x));
    DumpJitAddress(file, "x17",
                   static_cast<std::uintptr_t>(context->cpu_gprs[17].x));

    AzaharDynarmicJitBreadcrumb breadcrumb{};
    azahar_switch_dynarmic_jit_get_breadcrumb(&breadcrumb);
    std::fprintf(file, "breadcrumb_phase=%u\n",
                 breadcrumb.phase.load(std::memory_order_relaxed));
    std::fprintf(file, "breadcrumb_block_entry=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.block_entry));
    std::fprintf(file, "breadcrumb_callback_target=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.callback_target));
    std::fprintf(file, "breadcrumb_continuation=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.continuation));
    std::fprintf(file, "breadcrumb_dispatcher_target=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.dispatcher_target));
    std::fprintf(file, "breadcrumb_host_lr=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.host_lr));
    std::fprintf(file, "breadcrumb_host_sp=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.host_sp));
    std::fprintf(file, "breadcrumb_host_x16=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.host_x16));
    std::fprintf(file, "breadcrumb_host_x17=0x%016llx\n",
                 static_cast<unsigned long long>(breadcrumb.host_x17));
    std::fprintf(file, "breadcrumb_guest_pc=0x%08x\n", breadcrumb.guest_pc);
    std::fprintf(file, "breadcrumb_svc=0x%08x\n", breadcrumb.svc);
    DumpJitAddress(file, "breadcrumb_block_entry", breadcrumb.block_entry);
    DumpJitAddress(file, "breadcrumb_continuation", breadcrumb.continuation);
    DumpJitAddress(file, "breadcrumb_dispatcher_target", breadcrumb.dispatcher_target);

    std::fflush(file);
    std::fclose(file);
}

} // extern "C"
