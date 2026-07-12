// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

// Force-included only while compiling deko3d_rasterizer.cpp for the hybrid profile. Include the
// ordinary class declaration first so its virtual layout remains unchanged, then rename only the
// legacy out-of-class definition that appears later in that translation unit.
#include "video_core/renderer_deko3d/deko3d_rasterizer.h"
#define AccelerateDisplayTransfer AccelerateDisplayTransferLegacy
