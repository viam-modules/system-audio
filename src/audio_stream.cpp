#include "audio_stream.hpp"
#include "portaudio.h"
#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace microphone {

AudioStreamContext::AudioStreamContext(
    const vsdk::audio_info& audio_info,
    int buffer_duration_seconds)
    : audio_buffer(nullptr)
    , buffer_capacity(0)
    , info(audio_info)
    , stream_start_time()
    , first_sample_adc_time(0.0)
    , first_callback_captured(false)
    , total_samples_written(0)
{
    if (audio_info.sample_rate_hz <= 0) {
        VIAM_SDK_LOG(error) << "[AudioStreamContext] sample_rate_hz must be positive, got: "
                           << audio_info.sample_rate_hz;
        throw std::invalid_argument("sample_rate_hz must be positive");
    }
    if (audio_info.num_channels <= 0) {
        VIAM_SDK_LOG(error) << "[AudioStreamContext] num_channels must be positive, got: "
                           << audio_info.num_channels;
        throw std::invalid_argument("num_channels must be positive");
    }
    if (buffer_duration_seconds <= 0) {
        VIAM_SDK_LOG(error) << "[AudioStreamContext] buffer_duration_seconds must be positive, got: "
                           << buffer_duration_seconds;
        throw std::invalid_argument("buffer_duration_seconds must be positive");
    }

    // Pre-allocate circular buffer for N seconds of audio
    buffer_capacity = audio_info.sample_rate_hz * audio_info.num_channels * buffer_duration_seconds;

    if (buffer_capacity <= 0) {
        VIAM_SDK_LOG(error) << "[AudioStreamContext] buffer_capacity must be positive, calculated: "
                           << buffer_capacity;
        throw std::invalid_argument("buffer_capacity must be positive");
    }

    try {
        audio_buffer = std::make_unique<std::atomic<int16_t>[]>(buffer_capacity);
    } catch (const std::bad_alloc& e) {
        VIAM_SDK_LOG(error) << "[AudioStreamContext] Failed to allocate audio buffer of size "
                           << buffer_capacity << " samples: " << e.what();
        throw std::runtime_error("Failed to allocate audio buffer of size " +
                                 std::to_string(buffer_capacity) + " samples: " + e.what());
    }

    // Initialize all elements to 0
    for (int i = 0; i < buffer_capacity; i++) {
        audio_buffer[i].store(0);
    }
}

void AudioStreamContext::write_sample(int16_t sample) noexcept {
    // Find current index of circular buffer
    uint64_t pos = total_samples_written.load(std::memory_order_relaxed);
    int index = pos % buffer_capacity;

    audio_buffer[index].store(sample, std::memory_order_relaxed);

    // The memory_order_release ensures that reader threads that see this
    // counter value via acquire will also see the sample write above
    total_samples_written.fetch_add(1, std::memory_order_release);
}

int AudioStreamContext::read_samples(int16_t* buffer, int sample_count, uint64_t& read_position) noexcept {
    // memory_order_acquire synronizes with the release in write_sample,
    // ensuring all samples written up to the current_write_pos are visible
    uint64_t current_write_pos = total_samples_written.load(std::memory_order_acquire);

    // trying to read position that hasn't been written yet - return zero samples
    if(read_position > current_write_pos) {
        VIAM_SDK_LOG(warn) << "Read position " << read_position
                           << " is ahead of write position " << current_write_pos
                           << " - no samples available to read";
        return 0;
    }

    // Check if that sample is still in the buffer (not overwritten by new samples)
    if (current_write_pos > read_position + buffer_capacity) {
        // Position has been overwritten, skip to oldest available sample
        uint64_t old_position = read_position;
        read_position = current_write_pos - buffer_capacity;
        VIAM_SDK_LOG(warn) << "Audio buffer overrun: read position " << old_position
                           << " has been overwritten. Skipping to oldest available sample at "
                           << read_position << " (lost " << (read_position - old_position) << " samples)";
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

uint64_t AudioStreamContext::get_write_position() const noexcept {
    return total_samples_written.load(std::memory_order_acquire);
}

uint64_t AudioStreamContext::get_sample_number_from_timestamp(int64_t timestamp) noexcept{
    auto stream_start_ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(stream_start_time);
    int64_t stream_start_timestamp_ns = stream_start_ns.time_since_epoch().count();

    int64_t elapsed_time_ns = timestamp - stream_start_timestamp_ns;
    double elapsed_seconds = static_cast<double>(elapsed_time_ns) / NANOSECONDS_PER_SECOND;
    uint64_t sample_number = static_cast<uint64_t>(
        elapsed_seconds * info.sample_rate_hz * info.num_channels
    );
    return sample_number;
}

std::chrono::nanoseconds AudioStreamContext::calculate_sample_timestamp(
    uint64_t sample_number) noexcept
{
     // Convert sample_number to frame number (samples include all channels)
    uint64_t frame_number = sample_number / info.num_channels;
    uint64_t elapsed_ns = (frame_number * NANOSECONDS_PER_SECOND) / info.sample_rate_hz;

    auto elapsed_duration = std::chrono::nanoseconds(elapsed_ns);
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
// outputBuffer used for playback of audio - unused for microphone
int AudioCallback(const void *inputBuffer, void *outputBuffer,
                             unsigned long framesPerBuffer,
                             const PaStreamCallbackTimeInfo* timeInfo,
                             PaStreamCallbackFlags statusFlags,
                             void *userData)
{
    if (!userData) {
        // something wrong, stop stream
        return paAbort;
    }
    AudioStreamContext* ctx = static_cast<AudioStreamContext*>(userData);

    if (!ctx) {
        // something wrong, stop stream
        return paAbort;
    }

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
        ctx->stream_start_time = std::chrono::steady_clock::now();
        ctx->first_callback_captured.store(true);
    }

    int total_samples = framesPerBuffer * ctx->info.num_channels;

    for (int i = 0; i < total_samples; ++i) {
        ctx->write_sample(input[i]);
    }

    return paContinue;
}

std::chrono::nanoseconds calculate_sample_timestamp(
    const AudioStreamContext& ctx,
    uint64_t sample_number)
{
    // Convert sample_number to frame number (samples include all channels)
    uint64_t frame_number = sample_number / ctx.info.num_channels;
    uint64_t elapsed_ns = (frame_number * NANOSECONDS_PER_SECOND) / ctx.info.sample_rate_hz;

    auto elapsed_duration = std::chrono::nanoseconds(elapsed_ns);
    auto absolute_time = ctx.stream_start_time + elapsed_duration;

    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        absolute_time.time_since_epoch()
    );
}

int calculate_aligned_chunk_size(int sample_rate, int num_channels, int mp3_frame_size) {
    // Calculate how many frames fit into approximately 100-200ms
    // Target: around 150ms for reasonable latency

    double target_duration_seconds = 0.15;  // 150ms target
    double samples_per_channel_target = sample_rate * target_duration_seconds;

    // Round to nearest number of MP3 frames
    int num_frames = static_cast<int>(samples_per_channel_target / mp3_frame_size + 0.5);

    // Ensure at least 1 frame
    if (num_frames < 1) {
        num_frames = 1;
    }

    // Calculate total samples including all channels
    int samples_per_channel = num_frames * mp3_frame_size;
    int total_samples = samples_per_channel * num_channels;

    double actual_duration = static_cast<double>(samples_per_channel) / sample_rate;
    VIAM_SDK_LOG(debug) << "Calculated aligned chunk size: " << total_samples
                       << " samples (" << num_frames << " MP3 frames of " << mp3_frame_size << " samples, "
                       << actual_duration * 1000.0 << "ms, "
                       << sample_rate << "Hz, " << num_channels << " channels)";

    return total_samples;
}

} // namespace microphone
