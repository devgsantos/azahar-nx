// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "switch_nxlink.h"

#include <atomic>
#include <cstdio>
#include <unistd.h>

#include <switch/runtime/nxlink.h>

namespace Azahar::Switch::NxLink {
namespace {

std::atomic_int socket_fd{-1};

} // namespace

bool Initialize() {
    if (socket_fd.load(std::memory_order_acquire) >= 0) {
        return true;
    }

    const int fd = nxlinkStdioForDebug();
    if (fd < 0) {
        return false;
    }

    socket_fd.store(fd, std::memory_order_release);
    std::fprintf(stderr, "[Azahar.NxLink] connected fd=%d\n", fd);
    std::fflush(stderr);
    return true;
}

bool IsConnected() {
    return socket_fd.load(std::memory_order_acquire) >= 0;
}

void WriteLine(const char* message) {
    if (!IsConnected()) {
        return;
    }

    std::fputs(message ? message : "", stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

void Shutdown() {
    const int fd = socket_fd.exchange(-1, std::memory_order_acq_rel);
    if (fd < 0) {
        return;
    }

    std::fflush(stderr);
    close(fd);
}

} // namespace Azahar::Switch::NxLink
