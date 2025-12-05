#include "microphone.hpp"
#include "audio_utils.hpp"
#include "audio_stream.hpp"
#include "audio_codec.hpp"
#include "mp3_encoder.hpp"
#include <thread>
#include <algorithm>
#include <cctype>


namespace microphone {
using audio::codec::AudioCodec;

// === Static Helper Functions ===

// Calculate chunk size aligned to MP3 frame boundaries
// Returns the number of samples (including all channels) for an optimal chunk size
// mp3_frame_size should be the actual frame size from LAME (1152 or 576), defaults to 1152
int calculate_aligned_chunk_size(int sample_rate, int num_channels, int mp3_frame_size=1152) {
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

// Calculate chunk size based on codec and audio format
// For MP3, aligns chunk size with the mp3 frame size
static int calculate_chunk_size(const audio::codec::AudioCodec codec, int sample_rate, int num_channels, const MP3EncoderContext* mp3_ctx = nullptr) {
    if (codec == AudioCodec::MP3) {
        if (mp3_ctx == nullptr || mp3_ctx->frame_size == 0) {
            throw std::invalid_argument("MP3 encoder must be initialized before calculating chunk size");
        }
        // Use actual frame size from LAME
        return calculate_aligned_chunk_size(sample_rate, num_channels, mp3_ctx->frame_size);
    } else {
        // PCM codecs: 100ms chunks
        int num_samples_per_100_ms = static_cast<int>(sample_rate * 0.1);
        return num_samples_per_100_ms * num_channels;
    }
}

// RAII guard to automatically increment and decrement the stream counter
// during get_audio calls
class StreamGuard {
    std::mutex& mutex_;
    int& counter_;
public:
    StreamGuard(std::mutex& m, int& c) : mutex_(m), counter_(c) {
        std::lock_guard<std::mutex> lock(mutex_);
        counter_++;
    }
    ~StreamGuard() {
        std::lock_guard<std::mutex> lock(mutex_);
        counter_--;
    }
};


  // === Microphone Class Implementation ===

Microphone::Microphone(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg,
                       audio::portaudio::PortAudioInterface* pa)
    : viam::sdk::AudioIn(cfg.name()), stream_(nullptr), pa_(pa), active_streams_(0) {

    auto setup = audio::utils::setup_audio_device<audio::InputStreamContext>(
        cfg,
        audio::utils::StreamDirection::Input,
        AudioCallback,
        pa_
    );

    // Set new configuration and start stream under lock
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        device_name_ = setup.stream_params.device_name;
        device_index_ = setup.stream_params.device_index;
        sample_rate_ = setup.stream_params.sample_rate;
        num_channels_ = setup.stream_params.num_channels;
        latency_ = setup.stream_params.latency_seconds;
        audio_context_ = setup.audio_context;
        historical_throttle_ms_ = setup.config_params.historical_throttle_ms.value_or(DEFAULT_HISTORICAL_THROTTLE_MS);

        audio::utils::restart_stream(stream_, setup.stream_params, pa_);
    }
}

Microphone::~Microphone() {
       if (stream_) {
          PaError err = Pa_StopStream(stream_);
          if (err != paNoError) {
              VIAM_SDK_LOG(error) << "Failed to stop stream in destructor: "
                                 << Pa_GetErrorText(err);
          }

          err = Pa_CloseStream(stream_);
          if (err != paNoError) {
              VIAM_SDK_LOG(error) << "Failed to close stream in destructor: "
                                 << Pa_GetErrorText(err);
          }
    }
}

vsdk::Model Microphone::model("viam", "audio", "microphone");


std::vector<std::string> Microphone::validate(viam::sdk::ResourceConfig cfg) {
    auto attrs = cfg.attributes();

    if(attrs.count("device_name")) {
        if (!attrs["device_name"].is_a<std::string>()) {
            VIAM_SDK_LOG(error) << "[validate] device_name attribute must be a string";
            throw std::invalid_argument("device_name attribute must be a string");
        }
    }

    if(attrs.count("sample_rate")) {
        if (!attrs["sample_rate"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] sample_rate attribute must be a number";
            throw std::invalid_argument("sample_rate attribute must be a number");
        }
        double sample_rate = *attrs.at("sample_rate").get<double>();
        if (sample_rate <= 0) {
            VIAM_SDK_LOG(error) << "[validate] sample rate must be greater than zero";
            throw std::invalid_argument("sample rate must be greater than zero");
        }
    }
    if(attrs.count("num_channels")) {
        if (!attrs["num_channels"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] num_channels attribute must be a number";
            throw std::invalid_argument("num_channels attribute must be a number");
        }
        double num_channels = *attrs.at("num_channels").get<double>();
        if (num_channels <= 0) {
            VIAM_SDK_LOG(error) << "[validate] num_channels must be greater than zero";
            throw std::invalid_argument(" num_channels must be greater than zero");
        }
    }
    if(attrs.count("latency")) {
        if (!attrs["latency"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] latency attribute must be a number";
            throw std::invalid_argument("latency attribute must be a number");
        }
        double latency_ms = *attrs.at("latency").get<double>();
        if (latency_ms < 0) {
            VIAM_SDK_LOG(error) << "[validate] latency must be non-negative";
            throw std::invalid_argument("latency must be non-negative");
        }
    }

    if(attrs.count("historical_throttle_ms")) {
        if (!attrs["historical_throttle_ms"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] historical_throttle_ms attribute must be a number";
            throw std::invalid_argument("historical_throttle_ms attribute must be a number");
        }
        double historical_throttle_ms = *attrs.at("historical_throttle_ms").get<double>();
        if (historical_throttle_ms < 0) {
            VIAM_SDK_LOG(error) << "[validate] historical_throttle_ms must be non-negative";
            throw std::invalid_argument("historical_throttle_ms must be non-negative");
        }
    }
    return {};
}

void Microphone::reconfigure(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg) {
    VIAM_SDK_LOG(info) << "[reconfigure] Microphone reconfigure start";

    try {
        // Warn if reconfiguring with active streams
        // Changing the sample rate or number of channels mid stream
        // might cause issues client side, clients need to be actively
        // checking the audioinfo for changes. Changing these parameters
        // may also cause a small gap in audio.
        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            if (active_streams_ > 0) {
                VIAM_SDK_LOG(info) << "[reconfigure] Reconfiguring with " << active_streams_
                                   << " active stream(s). See README for reconfiguration considerations.";
            }
        }


        auto setup = audio::utils::setup_audio_device<audio::InputStreamContext>(
            cfg,
            audio::utils::StreamDirection::Input,
            AudioCallback,
            pa_
        );

        // Set new configuration and restart stream under lock
        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            device_name_ = setup.stream_params.device_name;
            device_index_ = setup.stream_params.device_index;
            sample_rate_ = setup.stream_params.sample_rate;
            num_channels_ = setup.stream_params.num_channels;
            latency_ = setup.stream_params.latency_seconds;
            audio_context_ = setup.audio_context;
            historical_throttle_ms_ = setup.config_params.historical_throttle_ms.value_or(DEFAULT_HISTORICAL_THROTTLE_MS);

            audio::utils::restart_stream(stream_, setup.stream_params, pa_);
        }
        VIAM_SDK_LOG(info) << "[reconfigure] Reconfigure completed successfully";
    } catch (const std::exception& e) {
        VIAM_SDK_LOG(error) << "[reconfigure] Reconfigure failed: " << e.what();
        throw;
    }
}

viam::sdk::ProtoStruct Microphone::do_command(const viam::sdk::ProtoStruct& command) {
    VIAM_SDK_LOG(error) << "do_command not implemented";
    return viam::sdk::ProtoStruct();
}

void Microphone::get_audio(std::string const& codec,
                           std::function<bool(vsdk::AudioIn::audio_chunk&& chunk)> const& chunk_handler,
                           double const& duration_seconds,
                           int64_t const& previous_timestamp,
                           const viam::sdk::ProtoStruct& extra) {

    VIAM_SDK_LOG(debug) << "get_audio called";

    // Parse codec string to enum
    AudioCodec codec_enum = audio::codec::parse_codec(codec);

    // guard to increment and decrement the active stream count
    StreamGuard stream_guard(stream_ctx_mu_, active_streams_);

    // Track audio duration using timestamps
    int64_t first_chunk_start_timestamp_ns = 0;
    bool duration_limit_set = false;

    uint64_t sequence = 0;

    // Track which context we're reading from to detect any device config changes
    std::shared_ptr<audio::InputStreamContext> stream_context;
    uint64_t read_position = 0;

    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        if (!audio_context_) {
            VIAM_SDK_LOG(error) << "Audio stream not initialized - audio_context_ is null";
            throw std::runtime_error("Audio stream not initialized");
        }
        stream_context = audio_context_;
    }

    // Initialize read position based on timestamp param
    read_position = get_initial_read_position(stream_context, previous_timestamp);

    // Get sample rate and channels - will be updated if context changes
    int stream_sample_rate = 0;
    int stream_num_channels = 0;
    int stream_historical_throttle_ms = 0;
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        stream_sample_rate = sample_rate_;
        stream_num_channels = num_channels_;
        stream_historical_throttle_ms = historical_throttle_ms_;
    }


    VIAM_SDK_LOG(info) << "throttle time" << stream_historical_throttle_ms;

    MP3EncoderContext mp3_ctx;
    uint64_t last_chunk_end_position;

    // Initialize MP3 encoder if needed
    if (codec_enum == AudioCodec::MP3) {
        initialize_mp3_encoder(mp3_ctx, stream_sample_rate, stream_num_channels);
    }

    // Calculate chunk size based on codec
    int samples_per_chunk = calculate_chunk_size(codec_enum, stream_sample_rate, stream_num_channels, &mp3_ctx);

    if (samples_per_chunk <= 0){
        std::ostringstream buffer;
        buffer << "calculated invalid samples_per_chunk: " << samples_per_chunk <<
        " with sample rate: " << stream_sample_rate << " num channels: " << stream_num_channels;
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }

    while (true) {
        // Check if audio_context_ changed (device reconfigured)
        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);

            // Detect context change (device reconfigured)
            if (audio_context_ != stream_context) {
                if (stream_context != nullptr) {
                    VIAM_SDK_LOG(info) << "Detected stream change (device reconfigure)";

                    // Update sample rate and channels from new config
                    stream_sample_rate = sample_rate_;
                    stream_num_channels = num_channels_;
                    stream_historical_throttle_ms = historical_throttle_ms_;


                    // Reinitialize MP3 encoder with new config if needed
                    if (codec_enum == AudioCodec::MP3) {
                        cleanup_mp3_encoder(mp3_ctx);
                        initialize_mp3_encoder(mp3_ctx, stream_sample_rate, stream_num_channels);
                        VIAM_SDK_LOG(info) << "Reinitialized MP3 encoder with new config";
                    }

                    // Recalculate chunk size using new config
                    samples_per_chunk = calculate_chunk_size(codec_enum, stream_sample_rate, stream_num_channels, &mp3_ctx);

                    if (samples_per_chunk <= 0){
                        std::ostringstream buffer;
                        buffer << "calculated invalid samples_per_chunk: " << samples_per_chunk <<
                        " with sample rate: " << stream_sample_rate << " num channels: " << stream_num_channels;
                        VIAM_SDK_LOG(error) << buffer.str();
                        throw std::runtime_error(buffer.str());
                    }
                }
                // Switch to new context and reset read position
                stream_context = audio_context_;
                read_position = stream_context->get_write_position();
                // Brief gap in audio, but stream continues
            }
        }

        // Check if we have enough samples for a full chunk
        uint64_t write_pos = stream_context->get_write_position();
        uint64_t available_samples = write_pos - read_position;

        // Wait until we have a full chunk worth of samples
        if (available_samples < samples_per_chunk) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        std::vector<int16_t> temp_buffer(samples_per_chunk);
        uint64_t chunk_start_position = read_position;
        // Read exactly one chunk worth of samples
        int samples_read = stream_context->read_samples(temp_buffer.data(), samples_per_chunk, read_position);

        if (samples_read < samples_per_chunk) {
            // Shouldn't happen since we checked available_samples, but to be safe
            VIAM_SDK_LOG(warn) << "Read fewer samples than expected: " << samples_read << " vs " << samples_per_chunk;
            continue;
        }

        vsdk::AudioIn::audio_chunk chunk;

        // Convert from int16 (captured format) to requested codec
        audio::codec::encode_audio_chunk(codec_enum, temp_buffer.data(), samples_read, chunk_start_position, mp3_ctx, chunk.audio_data);

        chunk.info.codec = codec;
        chunk.info.sample_rate_hz = stream_sample_rate;
        chunk.info.num_channels = stream_num_channels;
        chunk.sequence_number = sequence++;

        // Calculate timestamps based on sample position in stream
        uint64_t chunk_end_position = chunk_start_position + samples_read;
        if (codec_enum == AudioCodec::MP3 && mp3_ctx.encoder) {
            // Aadjust for encoder delay since decoded output will be shifted
            int delay_samples = mp3_ctx.encoder_delay * stream_num_channels;
            // Timestamps should reflect the data the encoder returned,
            // adjust for encoder delay
            if (chunk_start_position >= delay_samples) {
                chunk_start_position -= delay_samples;
            } else {
                chunk_start_position = 0;
            }
            chunk_end_position -= delay_samples;
        }

        chunk.start_timestamp_ns = stream_context->calculate_sample_timestamp(chunk_start_position);
        chunk.end_timestamp_ns = stream_context->calculate_sample_timestamp(chunk_end_position);

        last_chunk_end_position = chunk_end_position;

        // Set audio duration limit after first chunk (save the starting timestamp)
        if (!duration_limit_set && duration_seconds > 0) {
            first_chunk_start_timestamp_ns = chunk.start_timestamp_ns.count();
            duration_limit_set = true;
            VIAM_SDK_LOG(debug) << "Audio duration limit set: will read " << duration_seconds
                               << " seconds starting from timestamp " << first_chunk_start_timestamp_ns;
        }

        // Check if we've read enough audio (only if duration limit is set)
        if (duration_limit_set) {
            int64_t time_elapsed_ns = chunk.end_timestamp_ns.count() - first_chunk_start_timestamp_ns;
            double time_elapsed_seconds = time_elapsed_ns / 1e9;

            if (time_elapsed_seconds >= duration_seconds) {
                VIAM_SDK_LOG(debug) << "Reached audio duration limit: read " << time_elapsed_seconds
                                   << "s, limit was " << duration_seconds << "s";
                // Send final chunk before exiting
                chunk_handler(std::move(chunk));
                break;
            }
        }

        if (!chunk_handler(std::move(chunk))) {
            // If the chunk callback returned false, the stream has ended
            VIAM_RESOURCE_LOG(info) << "Chunk handler returned false, client disconnected";
            return;
        }

        // Check if we're reading historical data (far behind write position)
        if (previous_timestamp != 0) {
            VIAM_SDK_LOG(info) << "here!" << stream_historical_throttle_ms;
            uint64_t current_write_pos = stream_context->get_write_position();
            uint64_t distance_behind = current_write_pos - read_position;
            // If we're more than 1 second behind, we're reading historical data
            uint64_t one_second_samples = stream_sample_rate * stream_num_channels;
            if (distance_behind > one_second_samples) {
                 VIAM_SDK_LOG(info) << "sleeping" << stream_historical_throttle_ms;
                // Throttle historical data to give clients time to process
                std::this_thread::sleep_for(std::chrono::milliseconds(stream_historical_throttle_ms));
            }
        }
    }

    // Flush MP3 encoder at end of the stream to ensure all recorded audio
    // is returned
    if (codec_enum  == AudioCodec::MP3 && mp3_ctx.encoder) {
        std::vector<uint8_t> final_data;
        flush_mp3_encoder(mp3_ctx, final_data);

        if (!final_data.empty()) {
            size_t final_data_size = final_data.size();
            vsdk::AudioIn::audio_chunk final_chunk;
            final_chunk.audio_data = std::move(final_data);
            final_chunk.info.codec = codec;
            final_chunk.info.sample_rate_hz = stream_sample_rate;
            final_chunk.info.num_channels = stream_num_channels;
            final_chunk.sequence_number = sequence++;


            // Since our chunk sizes are aligned with the frame size,
            //there will be delay_samples flushed from the encoder buffer
            int delay_samples = mp3_ctx.encoder_delay * stream_num_channels;
            uint64_t timestamp_start = last_chunk_end_position;
            uint64_t timestamp_end = last_chunk_end_position + delay_samples;

            VIAM_SDK_LOG(debug) << "Flush: last_chunk_end=" << last_chunk_end_position
                              << " encoder_delay=" << mp3_ctx.encoder_delay << " samples (" << delay_samples << " total)"
                              << " timestamp_start=" << timestamp_start
                              << " timestamp_end=" << timestamp_end
                              << " flush_duration_samples=" << (timestamp_end - timestamp_start);

            final_chunk.start_timestamp_ns = stream_context->calculate_sample_timestamp(timestamp_start);
            final_chunk.end_timestamp_ns = stream_context->calculate_sample_timestamp(timestamp_end);

            chunk_handler(std::move(final_chunk));
            VIAM_SDK_LOG(debug) << "Sent final MP3 flush chunk with " << final_data_size << " bytes";
        }
    }

    VIAM_SDK_LOG(debug) << "get_audio stream completed";
}

viam::sdk::audio_properties Microphone::get_properties(const viam::sdk::ProtoStruct& extra){
    viam::sdk::audio_properties props;

    props.supported_codecs = {
        vsdk::audio_codecs::PCM_16,
        vsdk::audio_codecs::PCM_32,
        vsdk::audio_codecs::PCM_32_FLOAT,
        vsdk::audio_codecs::MP3
    };
    std::lock_guard<std::mutex> lock(stream_ctx_mu_);
    props.sample_rate_hz = sample_rate_;
    props.num_channels = num_channels_;

    return props;
}

std::vector<viam::sdk::GeometryConfig> Microphone::get_geometries(const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("get_geometries is unimplemented");
}

uint64_t get_initial_read_position(const std::shared_ptr<audio::InputStreamContext>& stream_context,
                                    int64_t previous_timestamp) {
    if (!stream_context) {
        throw std::invalid_argument("stream_context is null");
    }

    // default: start from current write position (most recent audio)
    if (previous_timestamp == 0) {
        return stream_context->get_write_position();
    }

    // Validate timestamp is non-negative
    if (previous_timestamp < 0) {
        std::ostringstream buffer;
        buffer << "Invalid previous_timestamp: " << previous_timestamp
                           << " (must be non-negative)";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::invalid_argument(buffer.str());;
    }

    // Validate timestamp is not before stream started
    auto stream_start_ns = std::chrono::time_point_cast<std::chrono::nanoseconds>(stream_context->stream_start_time);
    int64_t stream_start_timestamp_ns = stream_start_ns.time_since_epoch().count();
    if (previous_timestamp < stream_start_timestamp_ns) {
        std::ostringstream buffer;
        buffer << "Requested timestamp is before stream started: stream started at "
         << stream_start_timestamp_ns <<
        " requested: " << previous_timestamp;
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::invalid_argument(buffer.str());
    }

    // Convert timestamp to sample position, then advance by 1
    // We read from the NEXT sample after the requested timestamp
    uint64_t sample_number = stream_context->get_sample_number_from_timestamp(previous_timestamp);
    uint64_t read_position = sample_number + 1;

    // Validate timestamp is not in the future
    uint64_t current_write_pos = stream_context->get_write_position();
    if (read_position > current_write_pos) {
        // Calculate what the current time would be based on samples written
        auto latest_timestamp = stream_context->calculate_sample_timestamp(current_write_pos);
        std::ostringstream buffer;
        buffer << "requested timestamp " << previous_timestamp
               << " is in the future (latest available: " << latest_timestamp.count()
               << "): audio not yet captured";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::invalid_argument(buffer.str());
    }

    // Validate timestamp is not too old (audio has been overwritten)
    if (current_write_pos > read_position + stream_context->buffer_capacity) {
        std::ostringstream buffer;
        buffer << "requested timestamp is too old - audio has been overwritten. "
               << "Buffer only holds " << audio::BUFFER_DURATION_SECONDS << " seconds of audio history.";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::invalid_argument(buffer.str());
    }

    return read_position;
}

PaDeviceIndex findDeviceByName(const std::string& name, const audio::portaudio::PortAudioInterface& pa) {
    int deviceCount = pa.getDeviceCount();
     if (deviceCount < 0) {
          return paNoDevice;
      }

      for (PaDeviceIndex i = 0; i< deviceCount; i++) {
        const PaDeviceInfo* info = pa.getDeviceInfo(i);
         if (!info) {
            VIAM_SDK_LOG(warn) << "could not get device info for device index " << i << ", skipping";
            continue;
        }

        if (name == info->name) {
            // input and output devices can have the same name so check that it has input channels.
            if (info->maxInputChannels > 0) {
                return i;
            }
        }
    }
    return paNoDevice;

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
    audio::InputStreamContext* ctx = static_cast<audio::InputStreamContext*>(userData);

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
        ctx->stream_start_time = std::chrono::system_clock::now();
        ctx->first_callback_captured.store(true);
    }

    uint64_t total_samples = framesPerBuffer * ctx->info.num_channels;

    for (int i = 0; i < total_samples; ++i) {
        ctx->write_sample(input[i]);
    }

    return paContinue;
}

} // namespace microphone
