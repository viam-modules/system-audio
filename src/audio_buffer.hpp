#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <viam/sdk/common/audio.hpp>
#include <viam/sdk/components/audio_in.hpp>
#include "portaudio.h"

namespace audio {

namespace vsdk = ::viam::sdk;

constexpr int BUFFER_DURATION_SECONDS = 30;  // How much audio history to keep in buffer

// Base class for audio buffering - lock-free circular buffer with atomic operations
// Can be used by both input (microphone) and output (speaker) models.
// There is a 1:1 correspondence between AudioBuffer and viam audio resource
class AudioBuffer {
   public:
    AudioBuffer(const vsdk::audio_info& audio_info, int buffer_duration_seconds);
    virtual ~AudioBuffer() = default;

    // Writes an audio sample to the audio buffer
    void write_sample(int16_t sample) noexcept;

    // Read sample_count samples from the circular buffer starting at the inputted position
    int read_samples(int16_t* buffer, int sample_count, uint64_t& position) noexcept;

    uint64_t get_write_position() const noexcept;

    vsdk::audio_info info;
    int buffer_capacity;
    std::atomic<uint64_t> total_samples_written;
    std::unique_ptr<std::atomic<int16_t>[]> audio_buffer;
};

}  // namespace audio
