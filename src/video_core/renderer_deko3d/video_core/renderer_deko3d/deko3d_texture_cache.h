// Translation-unit-local resource lifetime guard for the native Deko3D rasterizer.
#pragma once
#include "../../deko3d_texture_cache.h"
#include "video_core/renderer_deko3d/deko3d_state.h"

#if defined(__SWITCH__) && defined(AZAHAR_DEKO3D_NATIVE_RUNTIME)
namespace VideoCore::Deko3D::NativeRuntime {
inline void DestroyRasterResourceAfterIdle(State& renderer_state, DkMemBlock block) {
    if (!block) {
        return;
    }
    renderer_state.WaitIdle();
    (dkMemBlockDestroy)(block);
}
} // namespace VideoCore::Deko3D::NativeRuntime

// Rasterizer methods have a State& member named state. Ensure depth-target replacement and final
// resource destruction cannot release a DkMemBlock still referenced by queued commands.
#define dkMemBlockDestroy(block) NativeRuntime::DestroyRasterResourceAfterIdle(state, (block))
#endif
