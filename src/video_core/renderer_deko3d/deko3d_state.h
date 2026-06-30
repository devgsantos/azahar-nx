// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <string>

#include "common/common_types.h"

#ifdef __SWITCH__
#include <deko3d.h>
#endif

namespace VideoCore::Deko3D {

class State {
public:
    static constexpr u32 FramebufferCount = 2;
    static constexpr u32 FramebufferWidth = 1280;
    static constexpr u32 FramebufferHeight = 720;
    static constexpr u32 CommandMemorySize = 64 * 1024;

    State();
    ~State();

    bool Initialize();
    void Shutdown();

    [[nodiscard]] bool IsInitialized() const {
        return initialized;
    }

    [[nodiscard]] const std::string& LastError() const {
        return last_error;
    }

    bool PresentClearFrame(float red, float green, float blue, float alpha);
    void WaitIdle();

private:
    void SetError(const char* message);

#ifdef __SWITCH__
    bool CreateDevice();
    bool CreateFramebuffers();
    bool CreateCommandBuffer();
    bool CreateQueue();
    bool RecordStaticCommands();

    DkDevice device{};
    DkQueue queue{};
    DkMemBlock framebuffer_mem_block{};
    DkImage framebuffers[FramebufferCount]{};
    DkSwapchain swapchain{};
    DkMemBlock cmdbuf_mem_block{};
    DkCmdBuf cmdbuf{};
    DkCmdList bind_framebuffer_cmds[FramebufferCount]{};
    DkCmdList clear_cmd{};
#endif

    bool initialized = false;
    std::string last_error;
};

} // namespace VideoCore::Deko3D
