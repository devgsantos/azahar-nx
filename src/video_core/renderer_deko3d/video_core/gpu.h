// Translation-unit-local overlay for native Deko3D presentation selection.
#pragma once

#include "../../gpu.h"
#include "video_core/renderer_deko3d/deko3d_native_runtime_state.h"

#if defined(__SWITCH__) && defined(AZAHAR_DEKO3D_NATIVE_RUNTIME)
// Prefer the newest GPU-authored color surface when a title renders offscreen and the current
// display-transfer implementation has not yet associated the final framebuffer address. Exact
// guest-address matching remains in use until the first native target is produced.
#define SelectPresentRenderTarget(address)                                                    \
    SelectPresentRenderTarget(NativeRuntime::GetLatestGpuColorAddress() != 0                  \
                                  ? NativeRuntime::GetLatestGpuColorAddress()                  \
                                  : (address))
#endif
