// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

namespace Azahar::Switch {

class SwitchAudio {
public:
    bool Initialize();
    void Shutdown();

private:
    bool initialized = false;
};

} // namespace Azahar::Switch
