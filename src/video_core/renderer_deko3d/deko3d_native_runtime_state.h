// Shared state used by the native Deko3D draw/present bridge.
#pragma once

#include <atomic>

#include "common/common_types.h"

namespace VideoCore::Deko3D::NativeRuntime {

inline std::atomic<PAddr> latest_gpu_color_address{0};

inline void SetLatestGpuColorAddress(PAddr address) {
    if (address != 0) {
        latest_gpu_color_address.store(address, std::memory_order_release);
    }
}

inline PAddr GetLatestGpuColorAddress() {
    return latest_gpu_color_address.load(std::memory_order_acquire);
}

} // namespace VideoCore::Deko3D::NativeRuntime
