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

namespace microphone {

namespace vsdk = ::viam::sdk;

/**
 * Opens a PortAudio input stream for audio recording.
 *
 * @param stream Pointer to receive the opened stream
 * @param deviceIndex PortAudio device index to use
 * @param channels Number of audio channels (1=mono, 2=stereo)
 * @param sampleRate Sample rate in Hz (e.g., 44100)
 * @param callback Callback function to process audio data
 * @param userData User data passed to callback
 * @param pa Optional PortAudio interface (for testing)
 */
void openStream(PaStream** stream, PaDeviceIndex deviceIndex, int channels, int sampleRate,
                PaStreamCallback* callback, void* userData = nullptr,
                audio::portaudio::PortAudioInterface* pa = nullptr);

void startStream(PaStream* stream, audio::portaudio::PortAudioInterface* pa= nullptr);
PaDeviceIndex findDeviceByName(const std::string& name, audio::portaudio::PortAudioInterface* pa= nullptr);
void shutdownStream(PaStream* stream, audio::portaudio::PortAudioInterface* pa= nullptr);


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

    // Member variables
    std::string device_name_;
    int sample_rate_;
    int num_channels_;
    static vsdk::Model model;

    // The mutex protects the stream and context
    std::mutex stream_ctx_mu_;
    PaStream* stream_;
    std::unique_ptr<AudioStreamContext> audio_context_;
    audio::portaudio::PortAudioInterface* pa_;
};

/**
 * Initializes the PortAudio library.
 * Must be called once before creating any Microphone instances.
 *
 * @param pa Optional PortAudio interface (for testing)
 */
void startPortAudio(audio::portaudio::PortAudioInterface* pa = nullptr);

} // namespace microphone
