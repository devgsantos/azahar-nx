// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

// This file is force-included only while compiling deko3d_state.cpp. Rename the public declaration
// and the legacy alias-only definition together in that one translation unit. Every other source
// keeps State::RecordDisplayTransfer and links it to deko3d_display_transfer_snapshot.cpp.
#define RecordDisplayTransfer RecordDisplayTransferAliasLegacy
#include "video_core/renderer_deko3d/deko3d_state.h"
