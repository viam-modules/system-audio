#pragma once

#include <viam/sdk/components/audio_in.hpp>
#include <viam/sdk/common/audio.hpp>
#include "portaudio.h"
#include <chrono>
#include <vector>
#include <mutex>
#include <atomic>

namespace audio {

namespace vsdk = ::viam::sdk;

constexpr int BUFFER_DURATION_SECONDS = 30;  // How much audio history to keep in buffer
constexpr double CHUNK_DURATION_SECONDS = 0.1;   // 100ms chunks (10 chunks per second)
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

    void reset() noexcept;

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
    InputStreamContext(const vsdk::audio_info& audio_info,
                      int buffer_duration_seconds = BUFFER_DURATION_SECONDS);

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
    std::atomic<int> playback_position;
    OutputStreamContext(const vsdk::audio_info& audio_info,
                       int buffer_duration_seconds = BUFFER_DURATION_SECONDS);
};


// Calculate chunk size aligned to MP3 frame boundaries
// Returns the number of samples (including all channels) for an optimal chunk size
// mp3_frame_size should be the actual frame size from LAME (1152 or 576), defaults to 1152
int calculate_aligned_chunk_size(int sample_rate, int num_channels, int mp3_frame_size = 1152);

/**
 * PortAudio callback function - runs on real-time audio thread.
 *
 * CRITICAL: This function must not:
 * - Allocate memory (malloc/new)
 * - Access the file system
 * - Call any functions that may block
 * - Take unpredictable amounts of time to complete
 *
 * From PortAudio docs: Do not allocate memory, access the file system,
 * call library functions or call other functions from the stream callback
 * that may block or take an unpredictable amount of time to complete.
 */
int AudioCallback(const void *inputBuffer, void *outputBuffer,
                  unsigned long framesPerBuffer,
                  const PaStreamCallbackTimeInfo* timeInfo,
                  PaStreamCallbackFlags statusFlags,
                  void *userData);

} // namespace audio
