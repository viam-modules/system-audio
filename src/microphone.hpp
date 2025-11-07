#pragma once

#include <viam/sdk/components/audio_in.hpp>
#include <viam/sdk/common/audio.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include "portaudio.h"
#include "portaudio.hpp"
#include "audio_stream.hpp"
#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <optional>

namespace microphone {
namespace vsdk = ::viam::sdk;

struct ConfigParams {
    std::string device_name;
    std::optional<int> sample_rate;  // optional: may use device default
    std::optional<int> num_channels;
    std::optional<double> latency_ms;
};

ConfigParams parseConfigAttributes(const viam::sdk::ResourceConfig& cfg);
PaDeviceIndex findDeviceByName(const std::string& name, audio::portaudio::PortAudioInterface* pa= nullptr);
void startPortAudio(audio::portaudio::PortAudioInterface* pa = nullptr);


class Microphone final : public viam::sdk::AudioIn, public viam::sdk::Reconfigurable {
public:
    Microphone(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg,
               audio::portaudio::PortAudioInterface* pa = nullptr);

    ~Microphone();

    static std::vector<std::string> validate(viam::sdk::ResourceConfig cfg);

    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command);

    // Get audio stream
    void get_audio(std::string const& codec,
                   std::function<bool(vsdk::AudioIn::audio_chunk&& chunk)> const& chunk_handler,
                   double const& duration_seconds,
                   int64_t const& previous_timestamp,
                   const viam::sdk::ProtoStruct& extra);

    viam::sdk::audio_properties get_properties(const viam::sdk::ProtoStruct& extra);
    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra);
    void reconfigure(const viam::sdk::Dependencies& deps, const viam::sdk::ResourceConfig& cfg);

    // internal functions, public for testing
    void openStream(PaStream** stream);
    void startStream(PaStream* stream);
    void shutdownStream(PaStream* stream);

private:
    void setupStreamFromConfig(const ConfigParams& params);

public:
    // Member variables
    std::string device_name_;
    PaDeviceIndex device_index_;
    int sample_rate_;
    int num_channels_;
    double latency_;
    static vsdk::Model model;

    // The mutex protects the stream and context pointer
    std::mutex stream_ctx_mu_;
    PaStream* stream_;
    std::shared_ptr<AudioStreamContext> audio_context_;  // shared_ptr allows safe reconfiguration
    audio::portaudio::PortAudioInterface* pa_;
    int active_streams_;  // Count of active get_audio calls
};


} // namespace microphone
