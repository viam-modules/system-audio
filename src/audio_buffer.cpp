#include "audio_buffer.hpp"
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <viam/sdk/common/audio.hpp>
#include <viam/sdk/components/audio_in.hpp>
#include "portaudio.h"

namespace audio {

namespace vsdk = ::viam::sdk;

AudioBuffer::AudioBuffer(const vsdk::audio_info& audio_info, int buffer_duration_seconds)
    : audio_buffer(nullptr), buffer_capacity(0), info(audio_info), total_samples_written(0) {
    if (audio_info.sample_rate_hz <= 0) {
        VIAM_SDK_LOG(error) << "[AudioBuffer] sample_rate_hz must be positive, got: " << audio_info.sample_rate_hz;
        throw std::invalid_argument("sample_rate_hz must be positive");
    }
    if (audio_info.num_channels <= 0) {
        VIAM_SDK_LOG(error) << "[AudioBuffer] num_channels must be positive, got: " << audio_info.num_channels;
        throw std::invalid_argument("num_channels must be positive");
    }
    if (buffer_duration_seconds <= 0) {
        VIAM_SDK_LOG(error) << "[AudioBuffer] buffer_duration_seconds must be positive, got: " << buffer_duration_seconds;
        throw std::invalid_argument("buffer_duration_seconds must be positive");
    }

    // Pre-allocate circular buffer for N seconds of audio
    buffer_capacity = audio_info.sample_rate_hz * audio_info.num_channels * buffer_duration_seconds;

    if (buffer_capacity <= 0) {
        VIAM_SDK_LOG(error) << "[AudioBuffer] buffer_capacity must be positive, calculated: " << buffer_capacity;
        throw std::invalid_argument("buffer_capacity must be positive");
    }

    try {
        audio_buffer = std::make_unique<std::atomic<int16_t>[]>(buffer_capacity);
    } catch (const std::bad_alloc& e) {
        VIAM_SDK_LOG(error) << "[AudioBuffer] Failed to allocate audio buffer of size " << buffer_capacity << " samples: " << e.what();
        throw std::runtime_error("Failed to allocate audio buffer of size " + std::to_string(buffer_capacity) + " samples: " + e.what());
    }

    // Initialize all elements to 0
    for (int i = 0; i < buffer_capacity; i++) {
        audio_buffer[i].store(0);
    }
}

void AudioBuffer::write_sample(int16_t sample) noexcept {
    // Find current index of circular buffer
    uint64_t pos = total_samples_written.load(std::memory_order_relaxed);
    int index = pos % buffer_capacity;

    audio_buffer[index].store(sample, std::memory_order_relaxed);

    // The memory_order_release ensures that reader threads that see this
    // counter value via acquire will also see the sample write above
    total_samples_written.fetch_add(1, std::memory_order_release);
}

int AudioBuffer::read_samples(int16_t* buffer, int sample_count, uint64_t& read_position) noexcept {
    // memory_order_acquire synronizes with the release in write_sample,
    // ensuring all samples written up to the current_write_pos are visible
    uint64_t current_write_pos = total_samples_written.load(std::memory_order_acquire);

    // trying to read position that hasn't been written yet - return zero samples
    if (read_position > current_write_pos) {
        VIAM_SDK_LOG(warn) << "Read position " << read_position << " is ahead of write position " << current_write_pos
                           << " - no samples available to read";
        return 0;
    }

    // Check if that sample is still in the buffer (not overwritten by new samples)
    if (current_write_pos > read_position + buffer_capacity) {
        // Position has been overwritten, skip to oldest available sample
        uint64_t old_position = read_position;
        read_position = current_write_pos - buffer_capacity;
        VIAM_SDK_LOG(warn) << "Audio buffer overrun: read position " << old_position
                           << " has been overwritten. Skipping to oldest available sample at " << read_position << " (lost "
                           << (read_position - old_position) << " samples)";
    }

    uint64_t available = current_write_pos - read_position;
    int to_read = std::min(static_cast<uint64_t>(sample_count), available);

    for (int i = 0; i < to_read; i++) {
        int index = (read_position + i) % buffer_capacity;
        buffer[i] = audio_buffer[index].load(std::memory_order_relaxed);
    }

    // update to the new position in the stream
    read_position += to_read;

    // return the actual number of samples read
    return to_read;
}

uint64_t AudioBuffer::get_write_position() const noexcept {
    return total_samples_written.load(std::memory_order_acquire);
}
}  // namespace audio
