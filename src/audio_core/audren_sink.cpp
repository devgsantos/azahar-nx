// Copyright Citra Emulator Project / Azahar Emulator Project
// Licensed under GPLv2 or any later version

#include "audio_core/audren_sink.h"

#ifdef __SWITCH__
#include <switch.h>
#include <malloc.h>
#endif

#include <cstring>
#include <vector>

#include "audio_core/audio_types.h"
#include "common/logging/log.h"

namespace AudioCore {

struct AudrenSink::Impl {
    static constexpr unsigned NumBuffers = 4;
    static constexpr std::size_t FramesPerBuffer = 480; // ~10 ms at 48 kHz
    static constexpr std::size_t Channels = 2;
    static constexpr std::size_t SamplesPerBuffer = FramesPerBuffer * Channels; // stereo samples
    static constexpr std::size_t BytesPerBuffer = SamplesPerBuffer * sizeof(s16);

#ifdef __SWITCH__
    AudioDriver driver{};
    bool driver_created = false;
    bool voice_started = false;

    // One contiguous mempool split into NumBuffers page-aligned blocks.
    void* mempool = nullptr;
    std::size_t mempool_size = 0;
    int mempool_id = -1;

    std::array<AudioDriverWaveBuf, NumBuffers> wavebufs{};
    std::array<std::atomic<bool>, NumBuffers> free_flags{};

    std::size_t current_buffer = 0;
    std::size_t current_offset = 0; // in stereo frames

    std::mutex mutex;
#endif
};

AudrenSink::AudrenSink(std::string_view device_id) : impl(std::make_unique<Impl>()) {
    (void)device_id;
#ifdef __SWITCH__
    static constexpr AudioRendererConfig config{
        AudioRendererOutputRate_48kHz,
        24,
        0,
        1,
        1,
        2,
    };

    const Result rc = audrvCreate(&impl->driver, &config, Impl::NumBuffers);
    if (R_FAILED(rc)) {
        LOG_ERROR(Audio_Sink, "audrvCreate failed: 0x{:08X}", rc);
        return;
    }
    impl->driver_created = true;

    // Align mempool to audren's required page size.
    constexpr std::size_t pool_size =
        ((Impl::NumBuffers * Impl::BytesPerBuffer + 0xFFF) / 0x1000) * 0x1000;
    impl->mempool = memalign(0x1000, pool_size);
    if (!impl->mempool) {
        LOG_ERROR(Audio_Sink, "Failed to allocate audren memory pool");
        return;
    }
    impl->mempool_size = pool_size;
    std::memset(impl->mempool, 0, pool_size);

    const int mempool_id = audrvMemPoolAdd(&impl->driver, impl->mempool, pool_size);
    if (mempool_id < 0) {
        LOG_ERROR(Audio_Sink, "audrvMemPoolAdd failed");
        return;
    }
    impl->mempool_id = mempool_id;
    audrvMemPoolAttach(&impl->driver, mempool_id);

    static constexpr u8 sink_channels[] = {0, 1};
    audrvDeviceSinkAdd(&impl->driver, AUDREN_DEFAULT_DEVICE_NAME, 2, sink_channels);

    audrvUpdate(&impl->driver);

    audrvVoiceInit(&impl->driver, 0, 2, PcmFormat_Int16, 48000);
    audrvVoiceSetDestinationMix(&impl->driver, 0, AUDREN_FINAL_MIX_ID);
    audrvVoiceSetMixFactor(&impl->driver, 0, 1.0f, 0, 0); // left  -> left
    audrvVoiceSetMixFactor(&impl->driver, 0, 1.0f, 1, 1); // right -> right

    for (std::size_t i = 0; i < Impl::NumBuffers; ++i) {
        u8* const base = static_cast<u8*>(impl->mempool) + i * Impl::BytesPerBuffer;
        impl->wavebufs[i].data_raw = base;
        impl->wavebufs[i].size = Impl::BytesPerBuffer;
        impl->wavebufs[i].start_sample_offset = 0;
        impl->wavebufs[i].end_sample_offset = Impl::SamplesPerBuffer;
        impl->free_flags[i].store(true);
    }

    audrvVoiceStart(&impl->driver, 0);
    impl->voice_started = true;

    LOG_INFO(Audio_Sink, "Audren sink initialized: {} buffers x {} frames @ 48000 Hz",
             Impl::NumBuffers, Impl::FramesPerBuffer);
#else
    LOG_INFO(Audio_Sink, "Audren sink constructed on non-Switch build (no-op)");
#endif
}

AudrenSink::~AudrenSink() {
#ifdef __SWITCH__
    if (!impl->driver_created) {
        return;
    }
    if (impl->voice_started) {
        audrvVoiceStop(&impl->driver, 0);
        impl->voice_started = false;
    }
    audrvUpdate(&impl->driver);
    audrvClose(&impl->driver);
    if (impl->mempool) {
        free(impl->mempool);
        impl->mempool = nullptr;
    }
#endif
}

unsigned int AudrenSink::GetNativeSampleRate() const {
    return 48000;
}

void AudrenSink::SetCallback(std::function<void(s16*, std::size_t)> callback) {
    // AudrenSink uses ImmediateSubmission and PushSamples; the callback path is unused.
    (void)callback;
}

void AudrenSink::PushSamples(const void* data, std::size_t num_samples) {
    if (!data || num_samples == 0) {
        return;
    }

#ifdef __SWITCH__
    if (!impl->driver_created || impl->mempool_id < 0) {
        return;
    }

    std::lock_guard<std::mutex> lock(impl->mutex);

    const s16* src = static_cast<const s16*>(data);
    std::size_t remaining = num_samples;

    while (remaining > 0) {
        // Recycle buffers that have finished playing.
        for (std::size_t i = 0; i < Impl::NumBuffers; ++i) {
            if (!impl->free_flags[i].load() &&
                impl->wavebufs[i].state == AudioDriverWaveBufState_Done) {
                impl->free_flags[i].store(true);
            }
        }

        if (impl->free_flags[impl->current_buffer].load()) {
            // Start a fresh buffer if we just recycled it.
            if (impl->current_offset == 0) {
                impl->wavebufs[impl->current_buffer].start_sample_offset = 0;
            }
        }

        // If current buffer is still in use, try to find a free one.
        if (!impl->free_flags[impl->current_buffer].load()) {
            std::size_t found = Impl::NumBuffers;
            for (std::size_t i = 0; i < Impl::NumBuffers; ++i) {
                if (impl->free_flags[i].load()) {
                    found = i;
                    break;
                }
            }
            if (found == Impl::NumBuffers) {
                // All buffers busy; drop the rest of this chunk to keep latency bounded.
                LOG_DEBUG(Audio_Sink, "Audren sink dropped {} samples (all buffers busy)",
                          remaining);
                break;
            }
            impl->current_buffer = found;
            impl->current_offset = 0;
        }

        const std::size_t space = Impl::FramesPerBuffer - impl->current_offset;
        const std::size_t to_copy = std::min(space, remaining);
        s16* const dst =
            static_cast<s16*>(const_cast<void*>(impl->wavebufs[impl->current_buffer].data_raw)) +
            impl->current_offset * 2;
        std::memcpy(dst, src, to_copy * 2 * sizeof(s16));

        src += to_copy * 2;
        impl->current_offset += to_copy;
        remaining -= to_copy;

        if (impl->current_offset >= Impl::FramesPerBuffer) {
            // Flush cache so the GPU/audio hardware sees the samples.
            armDCacheFlush(const_cast<void*>(impl->wavebufs[impl->current_buffer].data_raw),
                           Impl::BytesPerBuffer);

            impl->wavebufs[impl->current_buffer].start_sample_offset = 0;
            impl->wavebufs[impl->current_buffer].end_sample_offset =
                static_cast<u32>(Impl::FramesPerBuffer);
            impl->free_flags[impl->current_buffer].store(false);

            audrvVoiceAddWaveBuf(&impl->driver, 0,
                                 &impl->wavebufs[impl->current_buffer]);
            audrvUpdate(&impl->driver);

            // Advance to the next buffer.
            impl->current_buffer = (impl->current_buffer + 1) % Impl::NumBuffers;
            impl->current_offset = 0;
        }
    }
#else
    (void)data;
    (void)num_samples;
#endif
}

std::vector<std::string> ListAudrenSinkDevices() {
    return {"audren"};
}

} // namespace AudioCore
