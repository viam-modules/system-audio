#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <viam/sdk/common/audio.hpp>
#include <viam/sdk/components/audio_out.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include "audio_stream.hpp"
#include "portaudio.h"
#include "portaudio.hpp"

namespace speaker {
namespace vsdk = ::viam::sdk;

struct SpeakerStreamConfig {
    PaDeviceIndex device_index;
    int channels;
    int sample_rate;
    double latency = 0.0;
    PaStreamCallback* callback = nullptr;
    void* user_data = nullptr;
};

struct SpeakerConfigParams {
    std::string device_name;
    std::optional<int> sample_rate;
    std::optional<int> num_channels;
    std::optional<double> latency_ms;
};

int speakerCallback(const void* inputBuffer,
                    void* outputBuffer,
                    unsigned long framesPerBuffer,
                    const PaStreamCallbackTimeInfo* timeInfo,
                    PaStreamCallbackFlags statusFlags,
                    void* userData);

class Speaker final : public viam::sdk::AudioOut, public viam::sdk::Reconfigurable {
   public:
    Speaker(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg, audio::portaudio::PortAudioInterface* pa = nullptr);

    ~Speaker();

    static std::vector<std::string> validate(viam::sdk::ResourceConfig cfg);

    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command);

    void play(std::vector<uint8_t> const& audio_data, boost::optional<viam::sdk::audio_info> info, const viam::sdk::ProtoStruct& extra);

    viam::sdk::audio_properties get_properties(const viam::sdk::ProtoStruct& extra);
    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra);
    void reconfigure(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg);

    // Member variables
    std::string device_name_;
    double latency_;
    int sample_rate_;
    int num_channels_;
    static vsdk::Model model;
    std::mutex playback_mu_;

    PaStream* stream_;
    audio::portaudio::PortAudioInterface* pa_;

    // Protects stream_, audio_context_, and stream configuration
    std::mutex stream_mu_;

    // Audio context for speaker playback (includes buffer and playback position tracking)
    std::shared_ptr<audio::OutputStreamContext> audio_context_;
};

}  // namespace speaker
