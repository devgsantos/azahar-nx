// Translation-unit-local overlay for native Deko3D presentation selection.
#pragma once
#include "../../gpu.h"
#include "video_core/renderer_deko3d/deko3d_native_runtime_state.h"

#if defined(__SWITCH__) && defined(AZAHAR_DEKO3D_NATIVE_RUNTIME)
// Prefer the newest GPU-authored color surface when a game renders off-screen and has not yet
// exposed a matching CPU framebuffer address. This keeps native pixels visible while the full
// display-transfer cache is brought online. Exact matching remains the fallback when no native
// surface has been produced.
#define SelectPresentRenderTarget(address)                                                    \
    SelectPresentRenderTarget(NativeRuntime::GetLatestGpuColorAddress() != 0                  \
                                  ? NativeRuntime::GetLatestGpuColorAddress()                  \
                                  : (address))
#endif
