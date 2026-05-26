#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>
#include <viam/sdk/common/audio.hpp>
#include <viam/sdk/components/audio_out.hpp>
#include <viam/sdk/config/resource.hpp>
#include "audio_codec.hpp"
#include "audio_stream.hpp"
#include "audio_utils.hpp"
#include "portaudio.h"
#include "portaudio.hpp"
#include "watchdog.hpp"

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

class Speaker final : public viam::sdk::AudioOut {
   public:
    Speaker(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg, audio::portaudio::PortAudioInterface* pa = nullptr);

    ~Speaker();

    static std::vector<std::string> validate(viam::sdk::ResourceConfig cfg);

    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command);

    void play(std::vector<uint8_t> const& audio_data, boost::optional<viam::sdk::audio_info> info, const viam::sdk::ProtoStruct& extra);

    void play_stream(viam::sdk::audio_info info,
                     std::function<boost::optional<std::vector<uint8_t>>()> chunk_source,
                     const viam::sdk::ProtoStruct& extra) override;

    viam::sdk::audio_properties get_properties(const viam::sdk::ProtoStruct& extra);
    std::vector<viam::sdk::GeometryConfig> get_geometries(const viam::sdk::ProtoStruct& extra);

    // Member variables
    double latency_;
    std::optional<int> volume_;
    static vsdk::Model model;

    // This is used to ensure there is only one play() call at a time.
    std::mutex playback_mu_;

    PaStream* stream_;
    audio::portaudio::PortAudioInterface* pa_;

    // Protects stream_, audio_context_, and stream configuration
    std::mutex stream_mu_;

    // Audio context for speaker playback (includes buffer and playback position tracking)
    std::shared_ptr<audio::OutputStreamContext> audio_context_;

    // Flag to interrupt playback
    std::atomic<bool> stop_requested_{false};

    // Saved stream params so the watchdog can rebuild the stream with the same configuration.
    audio::utils::StreamParams stream_params_;

    // Device id from the resource config (empty if user configured by device_name or
    // system default). Used by restart_stalled_stream to re-resolve the device's current
    // PortAudio index, so we recover from kernel re-enumeration (e.g. USB unplug/replug).
    std::string device_id_;

    // Counts consecutive failed restart attempts; reset to 0 after a successful restart.
    // Once it reaches audio::utils::MAX_RESTART_ATTEMPTS, the watchdog
    // backs off to slow retries (audio::utils::BACKOFF_INTERVAL) instead of polling
    // every audio::utils::POLL_INTERVAL — supports hot-replug recovery without spamming
    // the kernel. The counter is capped at MAX so it doesn't grow unbounded.
    int restart_attempts_ = 0;

    // Background watchdog that polls audio_context_->last_callback_time_ns and triggers
    // restart_stalled_stream when the speaker callback has gone silent for too long,
    std::unique_ptr<audio::utils::StallWatchdog<audio::OutputStreamContext>> watchdog_;

   private:
    void restart_stalled_stream(const std::shared_ptr<audio::OutputStreamContext>& playback_context);

    // Decode (PCM_16/32/32_FLOAT), channel-convert, resample, and write into the playback
    // context's buffer. Returns samples written, or 0 if the stream context was swapped
    // mid-call (the only path that produces a 0 return). Caller must hold playback_mu_.
    size_t process_and_write_pcm(const uint8_t* data,
                                 size_t size,
                                 audio::codec::AudioCodec codec,
                                 int audio_sample_rate,
                                 int audio_num_channels,
                                 int speaker_sample_rate,
                                 int speaker_num_channels,
                                 std::shared_ptr<audio::OutputStreamContext> playback_context);

    void wait_for_playback(std::shared_ptr<audio::OutputStreamContext> playback_context,
                           uint64_t start_position,
                           uint64_t samples_to_drain);
};

}  // namespace speaker
