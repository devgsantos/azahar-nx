// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_shader.h"

#include "common/logging/log.h"
#include "common/switch_trace.h"

namespace VideoCore::Deko3D {

bool ShaderCache::Initialize() {
    SWITCH_TRACE_EVENT("Deko3D", "ShaderCache::Initialize", "enter");
    initialized = true;
    LOG_WARNING(Render,
                "Deko3D shader cache initialized without DKSH programs; hardware rasterizer "
                "pipeline is not active");
    SWITCH_TRACE_EVENT("Deko3D", "ShaderCache::Initialize",
                       "no DKSH shader programs loaded");
    SWITCH_TRACE_EVENT("Deko3D", "ShaderCache::Initialize", "leave");
    return true;
}

} // namespace VideoCore::Deko3D
