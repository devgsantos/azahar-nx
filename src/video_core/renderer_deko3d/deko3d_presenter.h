// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

namespace Core {
class System;
}

namespace VideoCore::Deko3D {

class State;

class Presenter {
public:
    Presenter(State& state, Core::System& system);

    bool PresentFrame();
    bool HasPresentedFrame() const {
        return presented_frame;
    }

private:
    State& state;
    Core::System& system;
    bool presented_frame = false;
};

} // namespace VideoCore::Deko3D
