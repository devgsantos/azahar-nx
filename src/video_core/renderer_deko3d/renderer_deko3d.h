// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include "video_core/renderer_base.h"
#include "video_core/renderer_deko3d/deko3d_presenter.h"
#include "video_core/renderer_deko3d/deko3d_rasterizer.h"
#include "video_core/renderer_deko3d/deko3d_shader.h"
#include "video_core/renderer_deko3d/deko3d_state.h"
#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

namespace VideoCore::Deko3D {

class RendererDeko3D final : public RendererBase {
public:
    RendererDeko3D(Core::System& system, Frontend::EmuWindow& window,
                   Frontend::EmuWindow* secondary_window);
    ~RendererDeko3D() override;

    RasterizerInterface* Rasterizer() override {
        return &rasterizer;
    }

    void SwapBuffers() override;
    void TryPresent(int timeout_ms, bool is_secondary) override;

    bool IsInitialized() const {
        return initialized;
    }

private:
    State state;
    TextureCache texture_cache;
    ShaderCache shader_cache;
    Deko3D::Rasterizer rasterizer;
    Presenter presenter;
    bool initialized = false;
};

} // namespace VideoCore::Deko3D
