// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version
// Refer to the license.txt file included.

#include "common/logging/log.h"
#include "common/settings.h"
#include "common/switch_trace.h"
#include "video_core/gpu.h"
#ifdef ENABLE_OPENGL
#include "video_core/renderer_opengl/renderer_opengl.h"
#endif
#ifdef ENABLE_SOFTWARE_RENDERER
#include "video_core/renderer_software/renderer_software.h"
#endif
#ifdef ENABLE_VULKAN
#include "video_core/renderer_vulkan/renderer_vulkan.h"
#endif
#ifdef AZAHAR_ENABLE_DEKO3D
#include "video_core/renderer_deko3d/renderer_deko3d.h"
#endif
#include "video_core/video_core.h"

#include <stdexcept>

#ifdef ENABLE_SDL2
#include <SDL.h>
#endif

namespace VideoCore {

std::unique_ptr<RendererBase> CreateRenderer(Frontend::EmuWindow& emu_window,
                                             Frontend::EmuWindow* secondary_window,
                                             Pica::PicaCore& pica, Core::System& system) {
#ifdef AZAHAR_SWITCH
    (void)pica;
    SWITCH_TRACE_EVENT("VideoCore", "CreateRenderer", "enter");
    SWITCH_TRACE_EVENT("VideoCore", "CreateRenderer", "force_backend_deko3d");
    LOG_INFO(Render, "Renderer backend selected: Deko3D");
#ifdef AZAHAR_ENABLE_DEKO3D
    auto renderer = std::make_unique<Deko3D::RendererDeko3D>(system, emu_window, secondary_window);
    if (!renderer || !renderer->IsInitialized() || !renderer->Rasterizer()) {
        SWITCH_TRACE_EVENT("VideoCore", "CreateRenderer", "failed_invalid_deko3d_renderer");
        throw std::runtime_error("Deko3D renderer initialization failed");
    }
    SWITCH_TRACE_EVENT("VideoCore", "CreateRenderer", "leave");
    return renderer;
#else
    SWITCH_TRACE_EVENT("VideoCore", "CreateRenderer", "failed_deko3d_not_compiled");
    throw std::runtime_error("Deko3D renderer initialization failed");
#endif
#else
    const auto graphics_api = Settings::GetWorkingGraphicsAPI();
    switch (graphics_api) {
#ifdef AZAHAR_ENABLE_DEKO3D
    case Settings::GraphicsAPI::Deko3D:
        return std::make_unique<Deko3D::RendererDeko3D>(system, emu_window, secondary_window);
#endif
#ifdef ENABLE_SOFTWARE_RENDERER
    case Settings::GraphicsAPI::Software:
        return std::make_unique<SwRenderer::RendererSoftware>(system, pica, emu_window);
#endif
#ifdef ENABLE_VULKAN
    case Settings::GraphicsAPI::Vulkan:
#if defined(ENABLE_SDL2) && !defined(__APPLE__)
        // TODO: When we migrate to SDL3, refactor so that we don't need to init here.
        if (SDL_WasInit(SDL_INIT_VIDEO) == 0) {
            SDL_Init(SDL_INIT_VIDEO);
        }
#endif // ENABLE_SDL2
        return std::make_unique<Vulkan::RendererVulkan>(system, pica, emu_window, secondary_window);
#endif
#ifdef ENABLE_OPENGL
    case Settings::GraphicsAPI::OpenGL:
        return std::make_unique<OpenGL::RendererOpenGL>(system, pica, emu_window, secondary_window);
#endif
    default:
        LOG_CRITICAL(Render,
                     "Unknown or unsupported graphics API {}, falling back to available default",
                     graphics_api);
#ifdef AZAHAR_ENABLE_DEKO3D
        return std::make_unique<Deko3D::RendererDeko3D>(system, emu_window, secondary_window);
#elif defined(ENABLE_OPENGL)
        return std::make_unique<OpenGL::RendererOpenGL>(system, pica, emu_window, secondary_window);
#elif ENABLE_VULKAN
        return std::make_unique<Vulkan::RendererVulkan>(system, pica, emu_window, secondary_window);
#elif ENABLE_SOFTWARE_RENDERER
        return std::make_unique<SwRenderer::RendererSoftware>(system, pica, emu_window);
#else
// TODO: Add a null renderer backend for this, perhaps.
#error "At least one renderer must be enabled."
#endif
    }
#endif
}

} // namespace VideoCore
