// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

namespace VideoCore::Deko3D {

class ShaderCache {
public:
    bool Initialize();
    bool IsInitialized() const {
        return initialized;
    }

private:
    bool initialized = false;
};

} // namespace VideoCore::Deko3D
