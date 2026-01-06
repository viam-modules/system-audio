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
#include "audio_utils.hpp"
#include "portaudio.h"
#include "portaudio.hpp"

namespace microphone {
namespace vsdk = ::viam::sdk;

constexpr double DEFAULT_HISTORICAL_THROTTLE_MS = 50;
PaDeviceIndex findDeviceByName(const std::string& name, const audio::portaudio::PortAudioInterface& pa);

// Calculates the initial read position from a previous timestamp
// Validates the timestamp and throws std::invalid_argument if:
//   - stream_context is null
//   - previous_timestamp < 0 (negative)
//   - previous_timestamp is before stream started
//   - previous_timestamp is in the future (audio not yet captured)
//   - previous_timestamp is too old (audio has been overwritten in circular buffer)
// Returns the write position if previous_timestamp == 0 (default: most recent audio)
uint64_t get_initial_read_position(const std::shared_ptr<audio::InputStreamContext>& stream_context, int64_t previous_timestamp);

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

    // Member variables
    std::string device_name_;
    PaDeviceIndex device_index_;
    int sample_rate_;            // Device's native sample rate (what stream is opened at)
    int requested_sample_rate_;  // User's requested sample rate (may differ from device rate)
    int num_channels_;
    double latency_;
    int historical_throttle_ms_;  // Throttle time for historical data stream
    static vsdk::Model model;

    // The mutex protects the stream, context, and the active streams counter
    std::mutex stream_ctx_mu_;
    PaStream* stream_;
    std::shared_ptr<audio::InputStreamContext> audio_context_;
    // This is null in production and used for testing to inject the mock portaudio functions
    const audio::portaudio::PortAudioInterface* pa_;
    // Count of active get_audio calls
    int active_streams_;
};

/**
 * PortAudio callback function - runs on real-time audio thread.
 *
 * CRITICAL: This function must not:
 * - Allocate memory (malloc/new)
 * - Access the file system
 * - Call any functions that may block
 * - Take unpredictable amounts of time to complete
 *
 * From PortAudio docs: Do not allocate memory, access the file system,
 * call library functions or call other functions from the stream callback
 * that may block or take an unpredictable amount of time to complete.
 */
int AudioCallback(const void* inputBuffer,
                  void* outputBuffer,
                  unsigned long framesPerBuffer,
                  const PaStreamCallbackTimeInfo* timeInfo,
                  PaStreamCallbackFlags statusFlags,
                  void* userData);

}  // namespace microphone
