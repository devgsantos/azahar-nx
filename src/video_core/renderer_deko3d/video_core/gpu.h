// Presenter-local overlay selecting the newest GPU-authored guest color surface.
#pragma once

#include "../../gpu.h"
#include "video_core/renderer_deko3d/deko3d_native_runtime_state.h"

#if defined(__SWITCH__) && defined(AZAHAR_DEKO3D_NATIVE_RUNTIME)
#define SelectPresentRenderTarget(address)                                                    \
    SelectPresentRenderTarget(NativeRuntime::GetLatestGpuColorAddress() != 0                  \
                                  ? NativeRuntime::GetLatestGpuColorAddress()                  \
                                  : (address))
#endif
