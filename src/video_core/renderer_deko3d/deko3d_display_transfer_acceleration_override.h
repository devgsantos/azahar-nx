// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

// Force-included only while compiling deko3d_rasterizer.cpp for the hybrid profile. Rename the
// legacy implementation so deko3d_display_transfer_acceleration.cpp can provide the public method.
// The renamed declaration is intentionally not an override in this one translation unit.
#define override
#define AccelerateDisplayTransfer AccelerateDisplayTransferLegacy
#include "video_core/renderer_deko3d/deko3d_rasterizer.h"
#undef override
