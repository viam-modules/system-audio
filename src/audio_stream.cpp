#include "audio_stream.hpp"
#include "portaudio.h"
#include <algorithm>
#include <cstring>

namespace microphone {

AudioStreamContext::AudioStreamContext(
    const vsdk::audio_info& audio_info,
    int samples_per_chunk,
    int buffer_duration_seconds)
    : info(audio_info)
    , samples_per_chunk(samples_per_chunk)
    , first_sample_adc_time(0.0)
    , first_callback_captured(false)
    , total_samples_written(0)
{
    // Pre-allocate circular buffer for N seconds of audio
    buffer_capacity = audio_info.sample_rate_hz * audio_info.num_channels * buffer_duration_seconds;

    // Allocate atomic buffer
    audio_buffer = std::make_unique<std::atomic<int16_t>[]>(buffer_capacity);

    // Initialize all elements to 0
    for (int i = 0; i < buffer_capacity; i++) {
        audio_buffer[i].store(0);
    }
}

void AudioStreamContext::write_sample(int16_t sample) {
    // Find current index of circular buffer
    uint64_t pos = total_samples_written.load(std::memory_order_relaxed);
    int index = pos % buffer_capacity;

    audio_buffer[index].store(sample, std::memory_order_relaxed);

    // The memory_order_release ensures that reader threads that see this
    // counter value via acquire will also see the sample write above
    total_samples_written.fetch_add(1, std::memory_order_release);
}

int AudioStreamContext::read_samples(int16_t* buffer, int sample_count, uint64_t& read_position) {
    // memory_order_acquire synronizes with the release in write_sample,
    // ensuring all samples written up to the current_write_pos are visible
    uint64_t current_write_pos = total_samples_written.load(std::memory_order_acquire);

    // trying to read position that hasn't been written yet - return zero samples
    if(read_position > current_write_pos) {
            return 0;
    }

    // Check if that sample is still in the buffer (not overwritten by new samples)
    if (current_write_pos > read_position + buffer_capacity) {
        // Position has been overwritten, skip to oldest available sample
        read_position = current_write_pos - buffer_capacity;
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

uint64_t AudioStreamContext::get_write_position() const {
    return total_samples_written.load(std::memory_order_acquire);
}

uint64_t AudioStreamContext::get_sample_number_from_timestamp(int64_t timestamp) {
    if (timestamp <= 0) {
        return 0;
    }

    auto stream_start_ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(stream_start_time);
    int64_t stream_start_timestamp_ns = stream_start_ns.time_since_epoch().count();

    // Check if timestamp is before stream started
    if (timestamp < stream_start_timestamp_ns) {
        return 0;
    }

    int64_t elapsed_time_ns = timestamp - stream_start_timestamp_ns;
    double elapsed_time_seconds = static_cast<double>(elapsed_time_ns) / 1e9;
    uint64_t sample_number = static_cast<uint64_t>(
        elapsed_time_seconds * static_cast<double>(info.sample_rate_hz) * info.num_channels
    );
    return sample_number;
}

std::chrono::nanoseconds AudioStreamContext::calculate_sample_timestamp(
    uint64_t sample_number)
{
    // Convert sample number to elapsed time
    double seconds_per_frame = 1.0 / static_cast<double>(info.sample_rate_hz);
    double elapsed_seconds = (sample_number / info.num_channels) * seconds_per_frame;

    auto elapsed_duration = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(elapsed_seconds)
    );

    auto absolute_time = stream_start_time + elapsed_duration;

    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        absolute_time.time_since_epoch()
    );
}


/**
 * PortAudio callback function - runs on real-time audio thread.
 *  This function must not:
 * - Allocate memory (malloc/new)
 * - Access the file system
 * - Call any functions that may block
 * - Take unpredictable amounts of time to complete
 *
 */
int AudioCallback(const void *inputBuffer, void *outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void *userData)
{
    AudioStreamContext* ctx = static_cast<AudioStreamContext*>(userData);

    if (inputBuffer == nullptr) {
        return paContinue;
    }

    const int16_t* input = static_cast<const int16_t*>(inputBuffer);

    // First callback: establish anchor between PortAudio time and wall-clock time
    if (!ctx->first_callback_captured.load()) {
        // the inputBufferADCTime describes the time when the
        // first sample of the input buffer was captured,
        // synced with the clock of the device
        ctx->first_sample_adc_time = timeInfo->inputBufferAdcTime;
        ctx->stream_start_time = std::chrono::system_clock::now();
        ctx->first_callback_captured.store(true);
    }

    int total_samples = framesPerBuffer * ctx->info.num_channels;

    for (int i = 0; i < total_samples; ++i) {
        ctx->write_sample(input[i]);
    }

    return paContinue;
}


} // namespace microphone
