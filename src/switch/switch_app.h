// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <string>
#include "switch_audio.h"
#include "switch_window.h"

namespace Azahar::Switch {

struct ExternalDataStatus;

class SwitchApp {
public:
    int Run();

private:
    bool InitializePlatform();
    int LaunchGame(const std::string& path);
    void DrawStatus(const char* message) const;
    void DrawFatal(const std::string& message) const;
    void DrawExternalDataWarning(const ExternalDataStatus& status) const;

    SwitchWindow window;
    SwitchAudio audio;
};

} // namespace Azahar::Switch
