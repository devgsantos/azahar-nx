// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

// This file is force-included only while compiling deko3d_state.cpp. Include the class declaration
// first, then rename the legacy alias-only implementation that appears later in that source file.
// Other translation units keep the public State::RecordDisplayTransfer declaration and link it to
// the snapshot implementation in deko3d_display_transfer_snapshot.cpp.
#include "video_core/renderer_deko3d/deko3d_state.h"

#define RecordDisplayTransfer RecordDisplayTransferAliasLegacy
