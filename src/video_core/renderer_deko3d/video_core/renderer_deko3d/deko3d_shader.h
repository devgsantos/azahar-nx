// Rasterizer-local overlay that publishes the latest GPU-authored guest color surface.
#pragma once

#include "../../deko3d_shader.h"
#include "video_core/renderer_deko3d/deko3d_native_runtime_state.h"

#if defined(__SWITCH__) && defined(AZAHAR_DEKO3D_NATIVE_RUNTIME)
#define MarkRenderTargetGpuDirty(target)                                                     \
    MarkRenderTargetGpuDirty(target);                                                        \
    NativeRuntime::SetLatestGpuColorAddress((target).key.color_address)
#endif
