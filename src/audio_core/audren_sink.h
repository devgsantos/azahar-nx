// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <mutex>
#include "audio_core/sink.h"

namespace AudioCore {

std::vector<std::string> ListAudrenSinkDevices();

class AudrenSink final : public Sink {
public:
    explicit AudrenSink(std::string_view device_id);
    ~AudrenSink() override;

    unsigned int GetNativeSampleRate() const override;

    bool ImmediateSubmission() override {
        return true;
    }

    void SetCallback(std::function<void(s16*, std::size_t)> callback) override;

    void PushSamples(const void* data, std::size_t num_samples) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace AudioCore
