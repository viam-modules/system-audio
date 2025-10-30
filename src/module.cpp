#include "module.hpp"
#include <thread>

namespace microphone {

Microphone::Microphone(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg,
                       audio::portaudio::PortAudioInterface* pa)
    : viam::sdk::AudioIn(cfg.name()), stream_(nullptr), pa_(pa) {

    auto attrs = cfg.attributes();
    if (attrs.count("device_name")) {
        device_name_ = *attrs.at("device_name").get<std::string>();
    }
    audio::portaudio::RealPortAudio real_pa;
    audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    PaDeviceIndex device_index;
    const PaDeviceInfo* deviceInfo;

    if (device_name_.empty()) {
        device_index = audio_interface.getDefaultInputDevice();
        if (device_index == paNoDevice) {
            throw std::runtime_error("no default input device found");
        }
        deviceInfo = audio_interface.getDeviceInfo(device_index);
        if (deviceInfo) {
            device_name_ = deviceInfo->name;  // Update device_name_ to actual device
        }
    } else {
        device_index = findDeviceByName(device_name_, &audio_interface);
        if (device_index == paNoDevice) {
            throw std::runtime_error("audio input device with name " + device_name_ + " not found");
        }
        deviceInfo = audio_interface.getDeviceInfo(device_index);
    }

    // Use device's default sample rate if not specified in config
    if (attrs.count("sample_rate")) {
        sample_rate_ = static_cast<int>(*attrs.at("sample_rate").get<double>());
    } else {
        sample_rate_ = static_cast<int>(deviceInfo->defaultSampleRate);
        VIAM_SDK_LOG(info) << "Using device default sample rate: " << sample_rate_;
    }

    if (attrs.count("num_channels")) {
        num_channels_ = static_cast<int>(*attrs.at("num_channels").get<double>());
    } else {
        num_channels_ = 1; // default to mono
    }

    // lock the mutex when making changes to the stream
    std::lock_guard<std::mutex> lock(stream_ctx_mu_);
    // Create audio context
    vsdk::audio_info info{vsdk::audio_codecs::PCM_16, sample_rate_, num_channels_};
    size_t samples_per_chunk = sample_rate_ / 10;  // 100ms chunks
    audio_context_ = std::make_unique<AudioStreamContext>(info, samples_per_chunk);

    openStream(&stream_, device_index, num_channels_, sample_rate_, AudioCallback, audio_context_.get(), pa);
    startStream(stream_, pa);

}

// Microphone destructor
Microphone::~Microphone() {
    if (stream_) {
        Pa_StopStream(stream_);
        Pa_CloseStream(stream_);
    }
}

// Static model definition
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
    }

    if(attrs.count("num_channels")) {
        if (!attrs["num_channels"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] num_channels attribute must be a number";
            throw std::invalid_argument("num_channels attribute must be a number");
        }
    }
    return {};
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

    //TODO: prev timestamp

    // Validate codec is supported
    if (codec != vsdk::audio_codecs::PCM_16) {
        throw std::invalid_argument("Unsupported codec: " + codec +
            ". Supported codecs: pcm16");
    }

    VIAM_SDK_LOG(info) << "get_audio called with codec: " << codec;

    auto start_time = std::chrono::steady_clock::now();
    auto end_time = start_time + std::chrono::milliseconds(
        static_cast<int64_t>(duration_seconds * 1000));

    // Track sequence number for this get_audio session (starts at 0)
    uint64_t sequence = 0;

    while (std::chrono::steady_clock::now() < end_time) {

        std::vector<vsdk::AudioIn::audio_chunk> chunks;
        {
            // must lock when receiving from the stream
            std::lock_guard<std::mutex> lock(stream_ctx_mu_);
            chunks = audio_context_->get_new_chunks();
        }

        for (auto& chunk : chunks) {
            chunk.sequence_number = sequence++;

            VIAM_SDK_LOG(info) << "Sending chunk with codec: " << codec;
            if (!chunk_handler(std::move(chunk))) {
                VIAM_RESOURCE_LOG(info) << "Chunk handler returned false, stopping";
                return;
            }
        }

        // small sleep to avoid busy wait
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    VIAM_SDK_LOG(info) << "get_audio completed";
}

viam::sdk::audio_properties Microphone::get_properties(const viam::sdk::ProtoStruct& extra){
    viam::sdk::audio_properties props;

    // Supported audio codecs
    props.supported_codecs = {
        vsdk::audio_codecs::PCM_16
    };

    // Return current configuration
    props.sample_rate_hz = sample_rate_;
    props.num_channels = num_channels_;

    return props;
}

std::vector<viam::sdk::GeometryConfig> Microphone::get_geometries(const viam::sdk::ProtoStruct& extra) {
    return std::vector<viam::sdk::GeometryConfig>();
}

void Microphone::reconfigure(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg) {
    VIAM_SDK_LOG(info) << "[reconfigure] Microphone reconfigure start";
    // Lock the stream for the entire reconfigure
    std::lock_guard<std::mutex> lock(stream_ctx_mu_);

    // Store previous configuration
    std::string prev_device_name = device_name_;
    int prev_sample_rate = sample_rate_;
    int prev_num_channels = num_channels_;

    // Parse new configuration
    auto attrs = cfg.attributes();
    std::string new_device_name = device_name_;
    if (attrs.count("device_name")) {
        new_device_name = *attrs.at("device_name").get<std::string>();
    }

    // Get device info to determine default sample rate if needed
    int new_sample_rate = sample_rate_;
    if (attrs.count("sample_rate")) {
        new_sample_rate = static_cast<int>(*attrs.at("sample_rate").get<double>());
    } else {
        // Use device's default sample rate
        audio::portaudio::RealPortAudio real_pa;
        audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;

        PaDeviceIndex device_index;
        if (new_device_name.empty()) {
            device_index = audio_interface.getDefaultInputDevice();
        } else {
            device_index = findDeviceByName(new_device_name, &audio_interface);
        }

        if (device_index != paNoDevice) {
            const PaDeviceInfo* deviceInfo = audio_interface.getDeviceInfo(device_index);
            if (deviceInfo) {
                new_sample_rate = static_cast<int>(deviceInfo->defaultSampleRate);
            }
        }
    }

    int new_num_channels = num_channels_;
    if (attrs.count("num_channels")) {
        new_num_channels = static_cast<int>(*attrs.at("num_channels").get<double>());
    } else {
        new_num_channels = 1; // default to mono
    }

    // Check if any configuration has changed
    bool config_changed = (prev_device_name != new_device_name) ||
                          (prev_sample_rate != new_sample_rate) ||
                          (prev_num_channels != new_num_channels);

    if (config_changed) {
        VIAM_SDK_LOG(info) << "[reconfigure] Configuration changed, restarting stream";
        VIAM_SDK_LOG(info) << "[reconfigure] Device: '" << prev_device_name << "' -> '" << new_device_name << "'";
        VIAM_SDK_LOG(info) << "[reconfigure] Sample rate: " << prev_sample_rate << " -> " << new_sample_rate;
        VIAM_SDK_LOG(info) << "[reconfigure] Channels: " << prev_num_channels << " -> " << new_num_channels;

        // Stop and close the existing stream
        if (stream_) {
            VIAM_SDK_LOG(info) << "[reconfigure] shutting down current stream";
            shutdownStream(stream_, pa_);
            stream_ = nullptr;
        }

        // Update configuration
        device_name_ = new_device_name;
        sample_rate_ = new_sample_rate;
        num_channels_ = new_num_channels;

        // Get device index for opening stream
        audio::portaudio::RealPortAudio real_pa;
        audio::portaudio::PortAudioInterface& audio_interface = pa_ ? *pa_ : real_pa;

        PaDeviceIndex new_device_index;
        if (new_device_name.empty()) {
            new_device_index = audio_interface.getDefaultInputDevice();
            if (new_device_index == paNoDevice) {
                throw std::runtime_error("no default input device found");
            }
        } else {
            new_device_index = findDeviceByName(new_device_name, &audio_interface);
            if (new_device_index == paNoDevice) {
                throw std::runtime_error("audio input device with name " + new_device_name + " not found");
            }
        }

        // Recreate audio context with new settings
        vsdk::audio_info info{vsdk::audio_codecs::PCM_16, sample_rate_, num_channels_};
        size_t samples_per_chunk = sample_rate_ / 10;  // 100ms chunks
        audio_context_->info = info;
        audio_context_->samples_per_chunk = samples_per_chunk;
        audio_context_->working_buffer.resize(samples_per_chunk * num_channels_);
        audio_context_->current_sample_count = 0; // reset partial chunk - this could result in a small loss of audio


        // Open and start new stream

        VIAM_SDK_LOG(info) << "[reconfigure] restarting stream";
        openStream(&stream_, new_device_index, num_channels_, sample_rate_, AudioCallback, audio_context_.get(), pa_);
        startStream(stream_, pa_);

        VIAM_SDK_LOG(info) << "[reconfigure] Stream restarted successfully";
    } else {
        VIAM_SDK_LOG(info) << "[reconfigure] Configuration unchanged, no restart needed";
    }

    VIAM_SDK_LOG(info) << "[reconfigure] Microphone reconfigure end";

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
}

void openStream(PaStream** stream, PaDeviceIndex deviceIndex, int channels, int sampleRate,
                PaStreamCallback* callback, void* userData,
                audio::portaudio::PortAudioInterface* pa) {

    audio::portaudio::RealPortAudio real_pa;
    audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    // Get device info
    const PaDeviceInfo* deviceInfo = audio_interface.getDeviceInfo(deviceIndex);
    if (!deviceInfo) {
        throw std::runtime_error("failed to get device info for device index " + std::to_string(deviceIndex));
    }

    VIAM_SDK_LOG(info) << "Device: " << deviceInfo->name
                         << ", Default sample rate: " << deviceInfo->defaultSampleRate
                         << ", Max input channels: " << deviceInfo->maxInputChannels;

    // Setup stream parameters
    PaStreamParameters params;
    params.device = deviceIndex;
    params.channelCount = channels;
    params.sampleFormat = paInt16;
    params.suggestedLatency = deviceInfo->defaultLowInputLatency;
    params.hostApiSpecificStreamInfo = nullptr;  // Must be NULL if not used

    VIAM_SDK_LOG(info) << "opening stream for device " << deviceInfo->name << " with sample rate " <<
    sampleRate;

    PaError err = audio_interface.openStream(
        stream,
        &params,              // input params
        NULL,                 // output params
        sampleRate,
        paFramesPerBufferUnspecified, // let portaudio pick the frames per buffer
        paClipOff,           // stream flags - disable clipping
        callback,
        userData             // user data - pass through to callback
    );

    if (err != paNoError) {
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
