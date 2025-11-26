#include "microphone.hpp"
#include "mp3_encoder.hpp"
#include <thread>
#include <algorithm>
#include <cctype>


namespace microphone {

  // === Static Helper Functions ===

enum class AudioCodec {
    PCM_16,
    PCM_32,
    PCM_32_FLOAT,
    MP3
};

// Convert string codec to enum
static AudioCodec parse_codec(const std::string& codec_str) {
    if (codec_str == vsdk::audio_codecs::PCM_32) {
        return AudioCodec::PCM_32;
    } else if (codec_str == vsdk::audio_codecs::PCM_32_FLOAT) {
        return AudioCodec::PCM_32_FLOAT;
    } else if (codec_str == vsdk::audio_codecs::MP3) {
        return AudioCodec::MP3;
    } else {
        return AudioCodec::PCM_16;
    }
}

static void convert_pcm16_to_pcm32(const int16_t* samples, int sample_count, std::vector<uint8_t>& output) {
    if (samples == nullptr || sample_count <= 0) {
        output.clear();
        return;
    }

    // Convert int16 to int32 (left shift by 16 to preserve volume)
    output.resize(sample_count * sizeof(int32_t));
    int32_t* out = reinterpret_cast<int32_t*>(output.data());
    for (int i = 0; i < sample_count; i++) {
        out[i] = static_cast<int32_t>(samples[i]) << 16;
    }
}

static void convert_pcm16_to_float32(const int16_t* samples, int sample_count, std::vector<uint8_t>& output) {
    if (samples == nullptr || sample_count <= 0) {
        output.clear();
        return;
    }

    // Convert int16 to float32 (normalize to range -1.0 to 1.0)
    output.resize(sample_count * sizeof(float));
    float* out = reinterpret_cast<float*>(output.data());
    for (int i = 0; i < sample_count; i++) {
        out[i] = static_cast<float>(samples[i]) * INT16_TO_FLOAT_SCALE;
    }
}

static void copy_pcm16(const int16_t* samples, int sample_count, std::vector<uint8_t>& output) {
    if (samples == nullptr || sample_count <= 0) {
        output.clear();
        return;
    }

    output.resize(sample_count * sizeof(int16_t));
    std::memcpy(output.data(), samples, sample_count * sizeof(int16_t));
}

static void encode_audio_chunk(
    AudioCodec codec,
    int16_t* samples,
    int sample_count,
    uint64_t chunk_start_position,
    MP3EncoderContext& mp3_ctx,
    std::vector<uint8_t>& output_data)
{
    switch (codec) {
        case AudioCodec::PCM_32:
            convert_pcm16_to_pcm32(samples, sample_count, output_data);
            break;
        case AudioCodec::PCM_32_FLOAT:
            convert_pcm16_to_float32(samples, sample_count, output_data);
            break;
        case AudioCodec::MP3:
            encode_samples_to_mp3(mp3_ctx, samples, sample_count, chunk_start_position, output_data);
            break;
        case AudioCodec::PCM_16:
        default:
            copy_pcm16(samples, sample_count, output_data);
            break;
    }
}

static std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Calculate chunk size based on codec and audio format
// For MP3, aligns chunk size with the mp3 frame size
static int calculate_chunk_size(const std::string& codec, int sample_rate, int num_channels, const MP3EncoderContext* mp3_ctx = nullptr) {
    if (codec == vsdk::audio_codecs::MP3) {
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
        auto params = parseConfigAttributes(cfg);
        setupStreamFromConfig(params);
    }

// Microphone destructor
Microphone::~Microphone() {
    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
    }
}

vsdk::Model Microphone::model("viam", "audio", "microphone");

ConfigParams parseConfigAttributes(const viam::sdk::ResourceConfig& cfg) {
    auto attrs = cfg.attributes();
    ConfigParams params;

    if (attrs.count("device_name")) {
        params.device_name = *attrs.at("device_name").get<std::string>();
    }

    if (attrs.count("sample_rate")) {
        params.sample_rate = static_cast<int>(*attrs.at("sample_rate").get<double>());
    }

    if (attrs.count("num_channels")) {
        params.num_channels = static_cast<int>(*attrs.at("num_channels").get<double>());
    }

    if (attrs.count("latency")) {
        params.latency_ms = *attrs.at("latency").get<double>();
    }

    return params;
}

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

        auto params = parseConfigAttributes(cfg);
        setupStreamFromConfig(params);
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

    std::string requested_codec = toLower(codec);

    // Validate codec is supported
    if (requested_codec != vsdk::audio_codecs::PCM_16 &&
        requested_codec != vsdk::audio_codecs::PCM_32 &&
        requested_codec != vsdk::audio_codecs::PCM_32_FLOAT &&
        requested_codec != vsdk::audio_codecs::MP3) {
        std::ostringstream buffer;
        buffer << "Unsupported codec: " << codec <<
        ". Supported codecs: pcm16, pcm32, pcm32_float, mp3";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::invalid_argument(buffer.str());
    }

    // Parse codec string to enum
    AudioCodec codec_enum = parse_codec(requested_codec);

    // guard to increment and decrement the active stream count
    StreamGuard stream_guard(stream_ctx_mu_, active_streams_);

    // Set duration timer
    auto start_time = std::chrono::steady_clock::time_point();
    auto end_time = std::chrono::steady_clock::time_point::max();
    bool timer_started = false;

    // Track audio duration (in samples) in addition to wall-clock time
    uint64_t first_chunk_start_position = 0;
    uint64_t total_samples_to_read = 0;
    bool duration_limit_set = false;

    uint64_t sequence = 0;

    // Track which context we're reading from to detect any device config changes
    std::shared_ptr<AudioStreamContext> stream_context;
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

    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        stream_sample_rate = sample_rate_;
        stream_num_channels = num_channels_;
    }

    MP3EncoderContext mp3_ctx;
    uint64_t last_chunk_end_position;

    // Initialize MP3 encoder if needed
    if (requested_codec == vsdk::audio_codecs::MP3) {
        initialize_mp3_encoder(mp3_ctx, stream_sample_rate, stream_num_channels);
    }

    // Calculate chunk size based on codec
    int samples_per_chunk = calculate_chunk_size(requested_codec, stream_sample_rate, stream_num_channels, &mp3_ctx);

    if (samples_per_chunk <= 0){
        std::ostringstream buffer;
        buffer << "calculated invalid samples_per_chunk: " << samples_per_chunk <<
        " with sample rate: " << stream_sample_rate << " num channels: " << stream_num_channels;
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }

    while (std::chrono::steady_clock::now() < end_time) {
        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);

            // Detect context change (device reconfigured)
            if (audio_context_ != stream_context) {
                if (stream_context != nullptr) {
                    VIAM_SDK_LOG(info) << "Detected stream change (device reconfigure)";

                    // Update sample rate and channels from new config
                    stream_sample_rate = sample_rate_;
                    stream_num_channels = num_channels_;

                    // Reinitialize MP3 encoder with new config if needed
                    if (requested_codec == vsdk::audio_codecs::MP3) {
                        cleanup_mp3_encoder(mp3_ctx);
                        initialize_mp3_encoder(mp3_ctx, stream_sample_rate, stream_num_channels);
                        VIAM_SDK_LOG(info) << "Reinitialized MP3 encoder with new config";
                    }

                    // Recalculate chunk size using new config
                    samples_per_chunk = calculate_chunk_size(requested_codec, stream_sample_rate, stream_num_channels, &mp3_ctx);

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
        encode_audio_chunk(codec_enum, temp_buffer.data(), samples_read, chunk_start_position, mp3_ctx, chunk.audio_data);

        chunk.info.codec = codec;
        chunk.info.sample_rate_hz = stream_sample_rate;
        chunk.info.num_channels = stream_num_channels;
        chunk.sequence_number = sequence++;

        // Calculate timestamps based on sample position in stream
        uint64_t chunk_end_position = chunk_start_position + samples_read;
        if (requested_codec == vsdk::audio_codecs::MP3 && mp3_ctx.encoder) {
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

        // Start duration timer after first chunk arrives
        if (!timer_started && duration_seconds > 0) {
            start_time = std::chrono::steady_clock::now();
            end_time = start_time + std::chrono::milliseconds(static_cast<int64_t>(duration_seconds * 1000));
            timer_started = true;
        }

        if (!chunk_handler(std::move(chunk))) {
            // If the chunk callback returned false, the stream has ended
            VIAM_RESOURCE_LOG(info) << "Chunk handler returned false, client disconnected";
            return;
        }
    }

    // Flush MP3 encoder at end of the stream to ensure all recorded audio
    // is returned
    if (codec == vsdk::audio_codecs::MP3 && mp3_ctx.encoder) {
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
            //t here will be delay_samples flushed from the encoder buffer
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

    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        active_streams_--;
    }
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

void Microphone::setupStreamFromConfig(const ConfigParams& params) {
    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;

    // Determine device and get device info
    std::string new_device_name = params.device_name;
    PaDeviceIndex device_index = paNoDevice;
    const PaDeviceInfo* deviceInfo = nullptr;

    if (new_device_name.empty()) {
        device_index = audio_interface.getDefaultInputDevice();
        if (device_index == paNoDevice) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] No default input device found";
            throw std::runtime_error("no default input device found");
        }
        deviceInfo = audio_interface.getDeviceInfo(device_index);
        if (!deviceInfo) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Failed to get device info for default device";
            throw std::runtime_error("failed to get device info for default device");
        }
        if (!deviceInfo->name) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Failed to get the name of the default device";
            throw std::runtime_error("failed to get the name of the default device");
        }
        new_device_name = deviceInfo->name;

    } else {
        device_index = findDeviceByName(new_device_name, audio_interface);
        if (device_index == paNoDevice) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Audio input device with name '"
                               << new_device_name << "' not found";
            throw std::runtime_error("audio input device with name " + new_device_name + " not found");
        }
        deviceInfo = audio_interface.getDeviceInfo(device_index);
        if (!deviceInfo) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Failed to get device info for device: "
                               << new_device_name;
            throw std::runtime_error("failed to get device info for device: " + new_device_name);
        }
    }


    // Resolve final values (use params if specified, otherwise device defaults)
    int new_sample_rate = params.sample_rate.value_or(static_cast<int>(deviceInfo->defaultSampleRate));
    int new_num_channels = params.num_channels.value_or(1);
    double new_latency = params.latency_ms.has_value()
        ? params.latency_ms.value() / 1000.0  // Convert ms to seconds
        : deviceInfo->defaultLowInputLatency;

    // Validate num_channels against device's max input channels
    if (new_num_channels > deviceInfo->maxInputChannels) {
        VIAM_SDK_LOG(error) << "Requested " << new_num_channels << " channels but device '"
                            << deviceInfo->name << "' only supports " << deviceInfo->maxInputChannels
                            << " input channels";
        throw std::invalid_argument("num_channels exceeds device's maximum input channels");
    }

    // Check if config unchanged (only for reconfigure, not initial setup)
    if (stream_) {
        ActiveStreamConfig current_config{device_name_, sample_rate_, num_channels_, latency_};
        ActiveStreamConfig new_config{new_device_name, new_sample_rate, new_num_channels, new_latency};

        if (current_config == new_config) {
            VIAM_SDK_LOG(info) << "[setupStreamFromConfig] Config unchanged, skipping stream restart";
            return;
        }
    }

    // This is initial setup, not reconfigure, start stream
    if (!stream_) {
        // Create audio context for initial setup
        vsdk::audio_info info{vsdk::audio_codecs::PCM_16, new_sample_rate, new_num_channels};
        auto new_audio_context = std::make_shared<AudioStreamContext>(info);

        // Set configuration under lock before opening stream
        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            device_name_ = new_device_name;
            device_index_ = device_index;
            sample_rate_ = new_sample_rate;
            num_channels_ = new_num_channels;
            latency_ = new_latency;
            audio_context_ = new_audio_context;
        }

        PaStream* new_stream = nullptr;
        // These will throw in case of error
        openStream(&new_stream);
        startStream(new_stream);

        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            stream_ = new_stream;
        }

        return;
    }

    // Config has changed, restart stream
    vsdk::audio_info info{vsdk::audio_codecs::PCM_16, new_sample_rate, new_num_channels};
    auto new_audio_context = std::make_shared<AudioStreamContext>(info);

    PaStream* old_stream = nullptr;
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        old_stream = stream_;
    }
    if (old_stream) shutdownStream(old_stream);

    // Set new configuration under lock (needed before openStream since it uses these)
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        device_name_ = new_device_name;
        device_index_ = device_index;
        sample_rate_ = new_sample_rate;
        num_channels_ = new_num_channels;
        latency_ = new_latency;
        audio_context_ = new_audio_context;
    }

    // Open and start new stream
    PaStream* new_stream = nullptr;
    openStream(&new_stream);
    startStream(new_stream);

    // Swap in new stream under lock
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        stream_ = new_stream;
    }

    VIAM_SDK_LOG(info) << "[setupStreamFromConfig] Stream configured successfully";

}
void startPortAudio(const audio::portaudio::PortAudioInterface* pa) {
    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;
    PaError err = audio_interface.initialize();
    if (err != 0) {
        std::ostringstream buffer;
        buffer << "failed to initialize PortAudio library: " << Pa_GetErrorText(err);
        VIAM_SDK_LOG(error) << "[startPortAudio] " << buffer.str();
        throw std::runtime_error(buffer.str());
    }
    int numDevices = Pa_GetDeviceCount();
    VIAM_SDK_LOG(info) << "Available input devices:";
      for (int i = 0; i < numDevices; i++) {
          const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
          if (!info) {
             VIAM_SDK_LOG(error) << "failed to get device info for " << i+1 << "th device";
             continue;
          }
          if (info->maxInputChannels > 0) {
              VIAM_SDK_LOG(info) << info->name << " default sample rate: " << info->defaultSampleRate
              << " max input channels: " << info->maxInputChannels;
          }
      }
}

void Microphone::openStream(PaStream** stream) {
    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;


    VIAM_SDK_LOG(debug) << "Opening stream for device '" << device_name_
                         << "' (index " << device_index_ << ")"
                         << " with sample rate: " << sample_rate_
                         << ", channels: " << num_channels_;


    // Setup stream parameters
    PaStreamParameters params;
    params.device = device_index_;
    params.channelCount = num_channels_;
    params.sampleFormat = paInt16;
    params.suggestedLatency = latency_;
    params.hostApiSpecificStreamInfo = nullptr;  // Must be NULL if not used


    PaError err = audio_interface.isFormatSupported(&params, nullptr, sample_rate_);
    if (err != paNoError) {
        std::ostringstream buffer;
        buffer << "Audio format not supported by device '" << device_name_
               << "' (index " << device_index_ << "): "
               << Pa_GetErrorText(err) << "\n"
               << "Requested configuration:\n"
               << "  - Sample rate: " << sample_rate_ << " Hz\n"
               << "  - Channels: " << num_channels_ << "\n"
               << "  - Format: 16-bit PCM\n"
               << "  - Latency: " << latency_ << " seconds";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }


     VIAM_SDK_LOG(info) << "Opening stream for device '" << device_name_
                       << "' (index " << device_index_ << ")"
                       << " with sample rate " << sample_rate_
                       << " and latency " << params.suggestedLatency << " seconds";

    err = audio_interface.openStream(
        stream,
        &params,              // input params
        NULL,                 // output params
        sample_rate_,
        paFramesPerBufferUnspecified, // let portaudio pick the frames per buffer
        paNoFlag,            // stream flags - enable default clipping behavior
        AudioCallback,
        audio_context_.get() // user data to pass through to callback
    );

    if (err != paNoError) {
        std::ostringstream buffer;
        buffer << "Failed to open audio stream for device '" << device_name_ << "': "
             << "' (index " << device_index_ << "): "
               << Pa_GetErrorText(err)
               << " (sample_rate=" << sample_rate_
               << ", channels=" << num_channels_
               << ", latency=" << params.suggestedLatency << "s)";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }
}

void Microphone::startStream(PaStream* stream) {
    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;

    PaError err = audio_interface.startStream(stream);
    if (err != paNoError) {
        audio_interface.closeStream(stream);
        VIAM_SDK_LOG(error) << "Failed to start audio stream: " << Pa_GetErrorText(err);
        throw std::runtime_error(std::string("Failed to start audio stream: ") + Pa_GetErrorText(err));
    }
}

uint64_t get_initial_read_position(const std::shared_ptr<AudioStreamContext>& stream_context,
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
               << "Buffer only holds " << BUFFER_DURATION_SECONDS << " seconds of audio history.";
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

void Microphone::shutdownStream(PaStream* stream) {
    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;

    PaError err = audio_interface.stopStream(stream);
    if (err < 0) {
        std::ostringstream buffer;
        buffer << "failed to stop the stream: " <<  Pa_GetErrorText(err);
        VIAM_SDK_LOG(error) << "[shutdownStream] " << buffer.str();
        throw std::runtime_error(buffer.str());
    }


    err = audio_interface.closeStream(stream);
    if (err < 0) {
        std::ostringstream buffer;
        buffer << "failed to close the stream: " <<  Pa_GetErrorText(err);
        VIAM_SDK_LOG(error) << "[shutdownStream] " << buffer.str();
        throw std::runtime_error(buffer.str());
    }
}

} // namespace microphone
