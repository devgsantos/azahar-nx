// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

namespace Azahar::Switch::NxLink {

// Connects stderr to the host that launched the NRO through nxlink.
// Safe to call repeatedly. Returns true only while a live socket is available.
bool Initialize();
bool IsConnected();

// Mirrors one already-formatted diagnostic line to the nxlink stderr stream.
// This is intentionally a no-op until Initialize() succeeds.
void WriteLine(const char* message);

// Closes the socket returned by libnx. Call before Network::Shutdown().
void Shutdown();

} // namespace Azahar::Switch::NxLink
