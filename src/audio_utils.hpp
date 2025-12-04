#pragma once

#include "portaudio.hpp"
#include <optional>
#include <string>
#include <sstream>

namespace audio {
namespace utils {

enum class StreamDirection {
    Input,
    Output
};

struct ConfigParams {
    std::string device_name;
    std::optional<int> sample_rate;
    std::optional<int> num_channels;
    std::optional<double> latency_ms;
    std::optional<int> historical_throttle_ms;
};

// Configuration for opening a PortAudio stream
struct StreamParams {
    PaDeviceIndex device_index;
    std::string device_name;
    int sample_rate;
    int num_channels;
    double latency_seconds;
    bool is_input;
    PaStreamCallback* callback;
    void* user_data;  // Points to AudioStreamContext* or PlaybackBuffer*
};

// Helper function to find device by name
inline PaDeviceIndex findDeviceByName(const std::string& name, const audio::portaudio::PortAudioInterface& pa) {
    int deviceCount = pa.getDeviceCount();
    if (deviceCount < 0) {
        return paNoDevice;
    }

    for (PaDeviceIndex i = 0; i < deviceCount; i++) {
        const PaDeviceInfo* info = pa.getDeviceInfo(i);
        if (!info) {
            VIAM_SDK_LOG(warn) << "could not get device info for device index " << i << ", skipping";
            continue;
        }

        if (name == info->name) {
            return i;
        }
    }
    return paNoDevice;
}


inline ConfigParams parseConfigAttributes(const viam::sdk::ResourceConfig& cfg) {
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

    if (attrs.count("historical_throttle_ms")) {
        params.historical_throttle_ms = *attrs.at("historical_throttle_ms").get<double>();
    }

    return params;
}

// Shared setup function that works for both microphone and speaker
inline StreamParams setupStreamFromConfig(
    const ConfigParams& params,
    StreamDirection direction,
    PaStreamCallback* callback,
    const audio::portaudio::PortAudioInterface* pa = nullptr) {

    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    std::string device_name = params.device_name;
    PaDeviceIndex device_index = paNoDevice;
    const PaDeviceInfo* deviceInfo = nullptr;

    StreamParams stream_params = StreamParams{};

    if (device_name.empty()) {
        if (direction == StreamDirection::Input) {
            device_index = audio_interface.getDefaultInputDevice();
        } else {
            device_index = audio_interface.getDefaultOutputDevice();
        }
        if (device_index == paNoDevice) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] No default device found";
            throw std::runtime_error("no default device found");
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
    } else {
        device_index = findDeviceByName(device_name, audio_interface);
        if (device_index == paNoDevice) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Audio device with name '"
                               << device_name << "' not found";
            throw std::runtime_error("audio device with name " + device_name + " not found");
        }
        deviceInfo = audio_interface.getDeviceInfo(device_index);
        if (!deviceInfo) {
            VIAM_SDK_LOG(error) << "[setupStreamFromConfig] Failed to get device info for device: "
                               << device_name;
            throw std::runtime_error("failed to get device info for device: " + device_name);
        }
    }

    stream_params.device_index = device_index;
    stream_params.device_name = deviceInfo->name;

    // Resolve final values (use params if specified, otherwise device defaults)
    stream_params.sample_rate = params.sample_rate.value_or(static_cast<int>(deviceInfo->defaultSampleRate));
    stream_params.num_channels = params.num_channels.value_or(1);

    // Use appropriate default latency based on direction
    double default_latency = (direction == StreamDirection::Input)
        ? deviceInfo->defaultLowInputLatency
        : deviceInfo->defaultLowOutputLatency;

    stream_params.latency_seconds = params.latency_ms.has_value()
        ? params.latency_ms.value() / 1000.0
        : default_latency;

    // Validate num_channels against device's max channels
    int max_channels = (direction == StreamDirection::Input)
        ? deviceInfo->maxInputChannels
        : deviceInfo->maxOutputChannels;

    if (stream_params.num_channels > max_channels) {
        VIAM_SDK_LOG(error) << "Requested " << stream_params.num_channels << " channels but device '"
                            << deviceInfo->name << "' only supports " << max_channels
                            << " channels";
        throw std::invalid_argument("num_channels exceeds device's maximum channels");
    }

    stream_params.is_input = (direction == StreamDirection::Input);
    stream_params.callback = callback;
    stream_params.user_data = nullptr;  // Caller will set this after creating the audio context

    VIAM_SDK_LOG(info) << "[setupStreamFromConfig] Stream configured successfully";
    return stream_params;
}

inline void openStream(
    PaStream*& stream,
    const StreamParams& params,
    const audio::portaudio::PortAudioInterface* pa = nullptr) {

    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    VIAM_SDK_LOG(debug) << "Opening stream for device '" << params.device_name
                         << "' (index " << params.device_index << ")"
                         << " with sample rate: " << params.sample_rate
                         << ", channels: " << params.num_channels;

    // Setup stream parameters
    PaStreamParameters stream_params;
    stream_params.device = params.device_index;
    stream_params.channelCount = params.num_channels;
    stream_params.sampleFormat = paInt16;
    stream_params.suggestedLatency = params.latency_seconds;
    stream_params.hostApiSpecificStreamInfo = nullptr;

    // Determine which parameter is input and which is output
    const PaStreamParameters* inputParams = params.is_input ? &stream_params : nullptr;
    const PaStreamParameters* outputParams = params.is_input ? nullptr : &stream_params;

    PaError err = audio_interface.isFormatSupported(inputParams, outputParams, params.sample_rate);
    if (err != paNoError) {
        std::ostringstream buffer;
        buffer << "Audio format not supported by device '" << params.device_name
               << "' (index " << params.device_index << "): "
               << Pa_GetErrorText(err) << "\n"
               << "Requested configuration:\n"
               << "  - Sample rate: " << params.sample_rate << " Hz\n"
               << "  - Channels: " << params.num_channels << "\n"
               << "  - Format: 16-bit PCM\n"
               << "  - Latency: " << params.latency_seconds << " seconds";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }

    VIAM_SDK_LOG(info) << "Opening stream for device '" << params.device_name
                       << "' (index " << params.device_index << ")"
                       << " with sample rate " << params.sample_rate
                       << " and latency " << stream_params.suggestedLatency << " seconds";

    err = audio_interface.openStream(
        &stream,
        inputParams,
        outputParams,
        params.sample_rate,
        paFramesPerBufferUnspecified, // let portaudio pick the frames per buffer
        paNoFlag,
        params.callback,
        params.user_data
    );

    if (err != paNoError) {
        std::ostringstream buffer;
        buffer << "Failed to open audio stream for device '" << params.device_name
               << "' (index " << params.device_index << "): "
               << Pa_GetErrorText(err)
               << " (sample_rate=" << params.sample_rate
               << ", channels=" << params.num_channels
               << ", latency=" << stream_params.suggestedLatency << "s)";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }
}

inline void startStream(
    PaStream* stream,
    const audio::portaudio::PortAudioInterface* pa = nullptr) {

    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    PaError err = audio_interface.startStream(stream);
    if (err != paNoError) {
        VIAM_SDK_LOG(error) << "Failed to start stream: " << Pa_GetErrorText(err);
        throw std::runtime_error("Failed to start stream: " + std::string(Pa_GetErrorText(err)));
    }
}

inline void shutdown_stream(
    PaStream* stream,
    const audio::portaudio::PortAudioInterface* pa = nullptr) {

    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    PaError err = audio_interface.stopStream(stream);
    if (err != paNoError) {
        std::ostringstream buffer;
        buffer << "failed to stop the stream: " <<  Pa_GetErrorText(err);
        VIAM_SDK_LOG(error) << "[shutdown_stream] " << buffer.str();
        throw std::runtime_error(buffer.str());
    }

    err = audio_interface.closeStream(stream);
    if (err != paNoError) {
        std::ostringstream buffer;
        buffer << "failed to close the stream: " <<  Pa_GetErrorText(err);
        VIAM_SDK_LOG(error) << "[shutdown_stream] " << buffer.str();
        throw std::runtime_error(buffer.str());
    }
}

inline void restart_stream(
    PaStream*& stream,
    const StreamParams& params,
    const audio::portaudio::PortAudioInterface* pa = nullptr) {

    audio::portaudio::RealPortAudio real_pa;
    const audio::portaudio::PortAudioInterface& audio_interface = pa ? *pa : real_pa;

    if (stream) {
        shutdown_stream(stream, pa);
        stream = nullptr;
    }

    openStream(stream, params, pa);

    try {
        startStream(stream, pa);
    } catch (...) {
        // If startStream fails, close the stream before re-throwing
        audio_interface.closeStream(stream);
        stream = nullptr;
        throw;
    }
}

} // namespace utils
} // namespace audio
