// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "video_core/renderer_deko3d/deko3d_texture_cache.h"

#include "common/logging/log.h"
#include "common/switch_trace.h"

namespace VideoCore::Deko3D {

bool TextureCache::Initialize() {
    SWITCH_TRACE_EVENT("Deko3D", "TextureCache::Initialize", "enter");
    initialized = true;
    LOG_WARNING(Render,
                "Deko3D texture cache initialized without GPU texture entries; software "
                "framebuffer presentation remains active");
    SWITCH_TRACE_EVENT("Deko3D", "TextureCache::Initialize",
                       "no GPU texture entries allocated");
    SWITCH_TRACE_EVENT("Deko3D", "TextureCache::Initialize", "leave");
    return true;
}

} // namespace VideoCore::Deko3D
