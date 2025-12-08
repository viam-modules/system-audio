#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
#include <viam/sdk/common/audio.hpp>
#include <viam/sdk/components/audio_in.hpp>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include "audio_stream.hpp"
#include "portaudio.h"
#include "portaudio.hpp"

namespace microphone {
namespace vsdk = ::viam::sdk;

constexpr double DEFAULT_HISTORICAL_THROTTLE_MS = 50;

struct ConfigParams {
    std::string device_name;
    std::optional<int> sample_rate;  // optional: may use device default
    std::optional<int> num_channels;
    std::optional<double> latency_ms;
    std::optional<int> historical_throttle_ms;  // Throttle time for historical data playback
};

struct ActiveStreamConfig {
    std::string device_name;
    int sample_rate;
    int num_channels;
    double latency;

    bool operator==(const ActiveStreamConfig& other) const {
        return std::tie(device_name, sample_rate, num_channels, latency) ==
               std::tie(other.device_name, other.sample_rate, other.num_channels, other.latency);
    }

    bool operator!=(const ActiveStreamConfig& other) const {
        return !(*this == other);
    }
};

ConfigParams parseConfigAttributes(const viam::sdk::ResourceConfig& cfg);
PaDeviceIndex findDeviceByName(const std::string& name, const audio::portaudio::PortAudioInterface& pa);
void startPortAudio(const audio::portaudio::PortAudioInterface* pa = nullptr);

// Calculates the initial read position from a previous timestamp
// Validates the timestamp and throws std::invalid_argument if:
//   - stream_context is null
//   - previous_timestamp < 0 (negative)
//   - previous_timestamp is before stream started
//   - previous_timestamp is in the future (audio not yet captured)
//   - previous_timestamp is too old (audio has been overwritten in circular buffer)
// Returns the write position if previous_timestamp == 0 (default: most recent audio)
uint64_t get_initial_read_position(const std::shared_ptr<AudioStreamContext>& stream_context, int64_t previous_timestamp);

class Microphone final : public viam::sdk::AudioIn, public viam::sdk::Reconfigurable {
   public:
    Microphone(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg, audio::portaudio::PortAudioInterface* pa = nullptr);

    ~Microphone();

    static std::vector<std::string> validate(viam::sdk::ResourceConfig cfg);

    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command);

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
    int historical_throttle_ms_;  // Throttle time for historical data stream
    static vsdk::Model model;

    // The mutex protects the stream, context, and the active streams counter
    std::mutex stream_ctx_mu_;
    PaStream* stream_;
    std::shared_ptr<AudioStreamContext> audio_context_;
    // This is null in production and used for testing to inject the mock portaudio functions
    const audio::portaudio::PortAudioInterface* pa_;
    // Count of active get_audio calls
    int active_streams_;
};

}  // namespace microphone
