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

    // Accessors for screen textures (used by Presenter for CPU framebuffer upload)
    [[nodiscard]] void* GetScreenDataBuffer() const {
        return screen_data_buffer;
    }

    [[nodiscard]] u32 GetScreenDataSize() const {
        return screen_data_buffer_size;
    }

    [[nodiscard]] const DkImage* GetTopScreenImage() const {
        return top_screen_image;
    }

    [[nodiscard]] const DkImage* GetBottomScreenImage() const {
        return bottom_screen_image;
    }

private:
    void SetError(const char* message);

#ifdef __SWITCH__
    bool CreateDevice();
    bool CreateFramebuffers();
    bool CreateCommandBuffer();
    bool CreateQueue();
    bool RecordStaticCommands();
    bool CreateScreenTextures();

    DkDevice device{};
    DkQueue queue{};
    DkMemBlock framebuffer_mem_block{};
    std::array<DkImage, FramebufferCount> framebuffers{};
    std::array<DkImageView, FramebufferCount> framebuffer_views{};
    std::array<const DkImage*, FramebufferCount> swapchain_images{};
    DkSwapchain swapchain{};
    DkMemBlock cmdbuf_mem_block{};
    DkCmdBuf cmdbuf{};
    std::array<DkCmdList, FramebufferCount> bind_framebuffer_cmds{};
    DkCmdList clear_cmd{};

    // Screen textures for CPU framebuffer display (400x240 top, 320x240 bottom)
    DkMemBlock screen_tex_mem_block{};
    DkImage* top_screen_image = nullptr;     // 400x240 RGBA8
    DkImage* bottom_screen_image = nullptr;  // 320x240 RGBA8
    DkImageView* top_screen_view = nullptr;
    DkImageView* bottom_screen_view = nullptr;

    // Screen texture data buffer for CPU framebuffer upload
    void* screen_data_buffer = nullptr;
    u32 screen_data_buffer_size = 0;
#endif

    bool initialized = false;
    std::string last_error;
};

} // namespace VideoCore::Deko3D
