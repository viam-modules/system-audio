#include "microphone.hpp"
#include <thread>

namespace microphone {

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

    // Parse device name
    if (attrs.count("device_name")) {
        params.device_name = *attrs.at("device_name").get<std::string>();
    }

    // Parse sample rate (optional)
    if (attrs.count("sample_rate")) {
        params.sample_rate = static_cast<int>(*attrs.at("sample_rate").get<double>());
    }

    // Parse num_channels (optional)
    if (attrs.count("num_channels")) {
        params.num_channels = static_cast<int>(*attrs.at("num_channels").get<double>());
    }

    // Parse latency in milliseconds (optional)
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
    }

    if(attrs.count("num_channels")) {
        if (!attrs["num_channels"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] num_channels attribute must be a number";
            throw std::invalid_argument("num_channels attribute must be a number");
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


    //
    // Warn if reconfiguring with active streams
    // Changing the sample rate or number of channels mid stream
    // might cause issues client side, clients need to be actively
    // checking the audioinfo for changes. Changing these parameters
    // may also cause a small gap in audio.
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        if (active_streams_ > 0) {
            VIAM_SDK_LOG(warn) << "[reconfigure] Reconfiguring with " << active_streams_
                               << " active stream(s). This may cause audio gaps or break "
                               << "encoded audio. Clients should monitor audio_info in chunks.";
        }
    }

    auto params = parseConfigAttributes(cfg);
    setupStreamFromConfig(params);

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

    //TODO: get audio starting from prev timestamp

    // Validate codec is supported
    if (codec != vsdk::audio_codecs::PCM_16) {
        throw std::invalid_argument("Unsupported codec: " + codec +
            ". Supported codecs: pcm16");
    }

    VIAM_SDK_LOG(info) << "get_audio called with codec: " << codec;

    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        active_streams_++;
    }

    // Set duration timer
    auto start_time = std::chrono::steady_clock::time_point();
    auto end_time = std::chrono::steady_clock::time_point::max();
    bool timer_started = false;

    uint64_t sequence = 0;

    // Track which context we're reading from to detect any device config changes
    std::shared_ptr<AudioStreamContext> stream_context;
    uint64_t read_position = 0;

    // Initialize read position to current write position to get most recent audio
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        if (audio_context_) {
            read_position = audio_context_->get_write_position();
            stream_context = audio_context_;
        }
    }

    // Get sample rate and channels - will be updated if context changes
    int stream_sample_rate;
    int stream_num_channels;
    int samples_per_chunk;

    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        stream_sample_rate = sample_rate_;
        stream_num_channels = num_channels_;
    }
    samples_per_chunk = (stream_sample_rate * CHUNK_DURATION_SECONDS) * stream_num_channels;

    while (std::chrono::steady_clock::now() < end_time) {
        // Get current context (may change if config changes)
        std::shared_ptr<AudioStreamContext> current_context;

        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            current_context = audio_context_;

            // Detect context change (device reconfigured)
            if (current_context != stream_context) {
                if (stream_context != nullptr) {
                    VIAM_SDK_LOG(info) << "Detected stream change (device reconfigure), resetting read position";

                    // Update sample rate and channels from new config
                    stream_sample_rate = sample_rate_;
                    stream_num_channels = num_channels_;
                    samples_per_chunk = (stream_sample_rate * CHUNK_DURATION_SECONDS) * stream_num_channels;
                    read_position = current_context->get_write_position();
                }
                stream_context = current_context;
                // Brief gap in audio, but stream continues
            }
        }

        // Check if we have enough samples for a full chunk
        uint64_t write_pos = current_context->get_write_position();
        uint64_t available_samples = write_pos - read_position;


        VIAM_SDK_LOG(info) << "avaliable samples: " << available_samples;

        // Wait until we have a full chunk worth of samples
        if (available_samples < samples_per_chunk) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // Allocate buffer for this chunk size
        std::vector<int16_t> temp_buffer(samples_per_chunk);
        uint64_t chunk_start_position = read_position;
        // Read exactly one chunk worth of samples
        int samples_read = current_context->read_samples(temp_buffer.data(), samples_per_chunk, read_position);

        if (samples_read < samples_per_chunk) {
            // Shouldn't happen since we checked available_samples, but to be safe
            VIAM_SDK_LOG(warn) << "Read fewer samples than expected: " << samples_read << " vs " << samples_per_chunk;
            continue;
        }

        // Create chunk
        vsdk::AudioIn::audio_chunk chunk;
        chunk.audio_data.resize(samples_read * sizeof(int16_t));
        std::memcpy(chunk.audio_data.data(), temp_buffer.data(), samples_read * sizeof(int16_t));

        chunk.info.codec = codec;
        chunk.info.sample_rate_hz = stream_sample_rate;
        chunk.info.num_channels = stream_num_channels;
        chunk.sequence_number = sequence++;

        // Calculate timestamps based on sample position in stream
        chunk.start_timestamp_ns = calculate_sample_timestamp(
            current_context.get(),
            chunk_start_position
        );
        chunk.end_timestamp_ns = calculate_sample_timestamp(
            current_context.get(),
            chunk_start_position + samples_read
        );

        // Start duration timer after first chunk arrives
        if (!timer_started && duration_seconds > 0) {
            start_time = std::chrono::steady_clock::now();
            end_time = start_time + std::chrono::milliseconds(static_cast<int64_t>(duration_seconds * 1000));
            timer_started = true;
        }

        if (!chunk_handler(std::move(chunk))) {
            VIAM_RESOURCE_LOG(info) << "Chunk handler returned false, stopping";
            // Decrement active stream count before returning
            {
                std::lock_guard<std::mutex> lock(stream_ctx_mu_);
                active_streams_--;
            }
            return;
        }
    }

    VIAM_SDK_LOG(info) << "get_audio stream completed";

    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        active_streams_--;
    }
}

viam::sdk::audio_properties Microphone::get_properties(const viam::sdk::ProtoStruct& extra){
    viam::sdk::audio_properties props;

    props.supported_codecs = {
        vsdk::audio_codecs::PCM_16
    };
    std::lock_guard<std::mutex> lock(stream_ctx_mu_);
    props.sample_rate_hz = sample_rate_;
    props.num_channels = num_channels_;

    return props;
}

std::vector<viam::sdk::GeometryConfig> Microphone::get_geometries(const viam::sdk::ProtoStruct& extra) {
    return std::vector<viam::sdk::GeometryConfig>();
}


void Microphone::setupStreamFromConfig(const ConfigParams& params) {
    audio::portaudio::RealPortAudio real_pa;
    audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;

    // Determine device and get device info
    std::string new_device_name = params.device_name.empty() ? device_name_ : params.device_name;
    PaDeviceIndex device_index;
    const PaDeviceInfo* deviceInfo;

    VIAM_SDK_LOG(info) << "new device " << new_device_name;

    if (new_device_name.empty()) {
        device_index = audio_interface.getDefaultInputDevice();
        if (device_index == paNoDevice) {
            throw std::runtime_error("no default input device found");
        }
        deviceInfo = audio_interface.getDeviceInfo(device_index);
        new_device_name = deviceInfo ? deviceInfo->name : "";
    } else {
        device_index = findDeviceByName(new_device_name, &audio_interface);
        if (device_index == paNoDevice) {
            throw std::runtime_error("audio input device with name " + new_device_name + " not found");
        }
        deviceInfo = audio_interface.getDeviceInfo(device_index);
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
        bool config_unchanged = (device_name_ == new_device_name) &&
                                (sample_rate_ == new_sample_rate) &&
                                (num_channels_ == new_num_channels) &&
                                (latency_ == new_latency);

        if (config_unchanged) {
            VIAM_SDK_LOG(info) << "[setupStreamFromConfig] Config unchanged, skipping stream restart";
            return;
        }
    }

    // This is initial setup, not reconfigure, start stream
    if (!stream_) {
        device_name_ = new_device_name;
        sample_rate_ = new_sample_rate;
        num_channels_ = new_num_channels;
        latency_ = new_latency;

        // Create audio context for initial setup
        vsdk::audio_info info{vsdk::audio_codecs::PCM_16, new_sample_rate, new_num_channels};
        int samples_per_chunk = new_sample_rate * CHUNK_DURATION_SECONDS;  // 100ms chunks
        audio_context_ = std::make_shared<AudioStreamContext>(info, samples_per_chunk);

        PaStream* new_stream = nullptr;
        StreamConfig stream_config{
            .device_index = device_index,
            .channels = new_num_channels,
            .sample_rate = new_sample_rate,
            .latency = new_latency,
            .callback = AudioCallback,
            .user_data = audio_context_.get()
         };

        openStream(&new_stream, stream_config, pa_);
        startStream(new_stream, pa_);

        {
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            stream_ = new_stream;
        }

        return;
    }

    // Config has changed, restart stream
    vsdk::audio_info info{vsdk::audio_codecs::PCM_16, new_sample_rate, new_num_channels};
    int samples_per_chunk = new_sample_rate * CHUNK_DURATION_SECONDS ;  // 100ms chunks
    auto new_audio_context = std::make_shared<AudioStreamContext>(info, samples_per_chunk);

    PaStream* old_stream = nullptr;
    std::shared_ptr<AudioStreamContext> old_context;
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        old_stream = stream_;
        old_context = audio_context_;  // keep old context alive for any current streams
    }
    if (old_stream) shutdownStream(old_stream, pa_);


    // Open and start new stream
    PaStream* new_stream = nullptr;
    StreamConfig stream_config{
        .device_index = device_index,
        .channels = new_num_channels,
        .sample_rate = new_sample_rate,
        .latency = new_latency,
        .callback = AudioCallback,
        .user_data = new_audio_context.get()
    };

    openStream(&new_stream, stream_config, pa_);
    startStream(new_stream, pa_);

    // Swap in new stream/context under lock
    {
        std::lock_guard<std::mutex> lock(stream_ctx_mu_);
        stream_ = new_stream;
        audio_context_ = new_audio_context;
        device_name_ = new_device_name;
        sample_rate_ = new_sample_rate;
        num_channels_ = new_num_channels;
        latency_ = new_latency;
    }

    VIAM_SDK_LOG(info) << "[setupStreamFromConfig] Stream configured successfully";

}


void startPortAudio(audio::portaudio::PortAudioInterface* pa) {
    audio::portaudio::RealPortAudio real_pa;
    audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    PaError err = audio_interface.initialize();
    if (err != 0) {
        std::ostringstream buffer;
        buffer << "failed to initialize PortAudio library: " << Pa_GetErrorText(err);
        throw std::runtime_error(buffer.str());
    }

    int numDevices = Pa_GetDeviceCount();
    VIAM_SDK_LOG(info) << "Available input devices:\n";

      for (int i = 0; i < numDevices; i++) {
          const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
          if (info->maxInputChannels > 0) {
              VIAM_SDK_LOG(info) << i << ": \"" << info->name << "\" ";
          }
      }
}

void openStream(PaStream** stream,
                const StreamConfig& config,
                audio::portaudio::PortAudioInterface* pa) {

    audio::portaudio::RealPortAudio real_pa;
    audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    // Get device info
    const PaDeviceInfo* deviceInfo = audio_interface.getDeviceInfo(config.device_index);
    if (!deviceInfo) {
        throw std::runtime_error("failed to get device info for device index " + std::to_string(config.device_index));
    }

    VIAM_SDK_LOG(info) << "Device: " << deviceInfo->name
                         << ", Default sample rate: " << deviceInfo->defaultSampleRate
                         << ", Max input channels: " << deviceInfo->maxInputChannels;

    // Setup stream parameters
    PaStreamParameters params;
    params.device = config.device_index;
    params.channelCount = config.channels;
    params.sampleFormat = paInt16;
    // Use provided latency, or device default if latency is 0.0
    params.suggestedLatency = (config.latency > 0.0) ? config.latency : deviceInfo->defaultLowInputLatency;
    params.hostApiSpecificStreamInfo = nullptr;  // Must be NULL if not used

    VIAM_SDK_LOG(info) << "opening stream for device " << deviceInfo->name << " with sample rate " <<
    config.sample_rate << " and suggested latency "  << params.suggestedLatency << " seconds";

    PaError err = audio_interface.openStream(
        stream,
        &params,              // input params
        NULL,                 // output params
        config.sample_rate,
        paFramesPerBufferUnspecified, // let portaudio pick the frames per buffer
        paClipOff,           // stream flags - disable clipping
        config.callback,
        config.user_data     // user data to pass through to callback
    );

    if (err != paNoError) {
        VIAM_SDK_LOG(error) << "failed to open audio stream: " << Pa_GetErrorText(err);
        std::ostringstream buffer;
        buffer << "failed to open audio stream: " << Pa_GetErrorText(err);
        throw std::runtime_error(buffer.str());
    }
}

void startStream(PaStream* stream, audio::portaudio::PortAudioInterface* pa) {
    audio::portaudio::RealPortAudio real_pa;
    audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    PaError err = audio_interface.startStream(stream);
    if (err != paNoError) {
        audio_interface.closeStream(stream);
        VIAM_SDK_LOG(error) << "Failed to start audio stream: " << Pa_GetErrorText(err);
        throw std::runtime_error(std::string("Failed to start audio stream: ") + Pa_GetErrorText(err));
    }
}

PaDeviceIndex findDeviceByName(const std::string& name, audio::portaudio::PortAudioInterface* pa) {
    int deviceCount = pa->getDeviceCount();
     if (deviceCount < 0) {
          return paNoDevice;
      }

      for (PaDeviceIndex i = 0; i< deviceCount; i++) {
        const PaDeviceInfo* info = pa->getDeviceInfo(i);

        if (name == info->name) {
            // input and output devices can have the same name so check that it has input channels.
            if (info->maxInputChannels > 0) {
                return i;
            }
        }
    }
    return paNoDevice;

}

void shutdownStream(PaStream* stream, audio::portaudio::PortAudioInterface* pa) {
    audio::portaudio::RealPortAudio real_pa;
    audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    PaError err = audio_interface.stopStream(stream);
    if (err < 0) {
        std::ostringstream buffer;
        buffer << "failed to stop the stream: " <<  Pa_GetErrorText(err);
        throw std::runtime_error(buffer.str());
    }


    err = audio_interface.closeStream(stream);
    if (err < 0) {
        std::ostringstream buffer;
        buffer << "failed to close the stream: " <<  Pa_GetErrorText(err);
        throw std::runtime_error(buffer.str());
    }
}

} // namespace microphone
