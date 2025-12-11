#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <viam/sdk/common/audio.hpp>
#include <viam/sdk/components/audio_in.hpp>
#include "audio_buffer.hpp"
#include "portaudio.h"

namespace audio {

namespace vsdk = ::viam::sdk;

constexpr uint64_t NANOSECONDS_PER_SECOND = 1000000000ULL;
constexpr float INT16_TO_FLOAT_SCALE = 1.0f / 32768.0f;  // Scale factor for converting int16 samples to float [-1.0, 1.0]

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
