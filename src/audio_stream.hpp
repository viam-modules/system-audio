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

constexpr int BUFFER_DURATION_SECONDS = 30;     // How much audio history to keep in buffer
constexpr double CHUNK_DURATION_SECONDS = 0.1;  // 100ms chunks (10 chunks per second)
constexpr uint64_t NANOSECONDS_PER_SECOND = 1000000000ULL;
constexpr float INT16_TO_FLOAT_SCALE = 1.0f / 32768.0f;  // Scale factor for converting int16 samples to float [-1.0, 1.0]

// Base class for audio buffering - lock-free circular buffer with atomic operations
// Can be used by both input (microphone) and output (speaker) models.
class AudioBuffer {
   public:
    AudioBuffer(const vsdk::audio_info& audio_info, int buffer_duration_seconds);
    virtual ~AudioBuffer() = default;

    // Writes an audio sample to the audio buffer
    void write_sample(int16_t sample) noexcept;

    // Read sample_count samples from the circular buffer starting at the inputted position
    int read_samples(int16_t* buffer, int sample_count, uint64_t& position) noexcept;

    uint64_t get_write_position() const noexcept;

    void clear() noexcept;

    vsdk::audio_info info;
    int buffer_capacity;
    std::atomic<uint64_t> total_samples_written;

   protected:
    std::unique_ptr<std::atomic<int16_t>[]> audio_buffer;
};

// InputStreamContext manages a circular buffer of audio for microphone input
// Extends AudioBuffer with timestamp tracking for accurate audio capture metadata
class InputStreamContext : public AudioBuffer {
   public:
    InputStreamContext(const vsdk::audio_info& audio_info, int buffer_duration_seconds = BUFFER_DURATION_SECONDS);

    std::chrono::system_clock::time_point stream_start_time;
    double first_sample_adc_time;
    std::atomic<bool> first_callback_captured;
    std::chrono::nanoseconds calculate_sample_timestamp(uint64_t sample_number) noexcept;
    uint64_t get_sample_number_from_timestamp(int64_t timestamp) noexcept;
};

// OutputStreamContext manages a circular buffer of audio for speaker output
// Extends AudioBuffer with playback position tracking
class OutputStreamContext : public AudioBuffer {
   public:
    std::atomic<uint64_t> playback_position;
    OutputStreamContext(const vsdk::audio_info& audio_info, int buffer_duration_seconds = BUFFER_DURATION_SECONDS);
};

}  // namespace audio
