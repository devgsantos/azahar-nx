// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

namespace VideoCore::Deko3D {

class State;

class Presenter {
public:
    explicit Presenter(State& state);

    bool PresentFrame();
    bool HasPresentedFrame() const {
        return presented_frame;
    }

private:
    State& state;
    bool presented_frame = false;
};

} // namespace VideoCore::Deko3D
