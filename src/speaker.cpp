#include "speaker.hpp"
#include "audio_utils.hpp"
#include <viam/sdk/common/exception.hpp>
#include <viam/sdk/registry/registry.hpp>
#include <viam/sdk/components/audio_out.hpp>
#include <cstring>
#include <stdexcept>
#include <thread>

namespace speaker {
namespace vsdk = ::viam::sdk;


Speaker::Speaker(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg,
                       audio::portaudio::PortAudioInterface* pa)
    : viam::sdk::AudioOut(cfg.name()), pa_(pa), stream_(nullptr) {
        auto params = audio::utils::parseConfigAttributes(cfg);
        auto stream_params = audio::utils::setupStreamFromConfig(params,
                            audio::utils::StreamDirection::Output,
                            speakerCallback,
                            pa_);

    // Create audio context with actual sample rate/channels from params
    vsdk::audio_info info{vsdk::audio_codecs::PCM_16, stream_params.sample_rate, stream_params.num_channels};
    auto new_audio_context = std::make_shared<audio::OutputStreamContext>(info, 30);  // 30 second buffer for speaker

    // Set user_data to point to the audio context
    stream_params.user_data = new_audio_context.get();

    // Set new configuration and start stream under lock
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        sample_rate_ = stream_params.sample_rate;
        num_channels_ = stream_params.num_channels;
        latency_ = stream_params.latency_seconds;
        audio_context_ = new_audio_context;
        audio::utils::restart_stream(stream_, stream_params, pa_);
    }


}

Speaker::~Speaker() {
    if (stream_) {
          PaError err = Pa_StopStream(stream_);
          if (err != paNoError) {
              VIAM_SDK_LOG(error) << "Failed to stop stream in destructor: "
                                 << Pa_GetErrorText(err);
          }

          err = Pa_CloseStream(stream_);
          if (err != paNoError) {
              VIAM_SDK_LOG(error) << "Failed to close stream in destructor: "
                                 << Pa_GetErrorText(err);
          }
      }
}

vsdk::Model Speaker::model = {"viam", "audio", "speaker"};

/**
 * PortAudio callback function - runs on real-time audio thread.
 *  This function must not:
 * - Allocate memory (malloc/new)
 * - Access the file system
 * - Call any functions that may block
 * - Take unpredictable amounts of time to complete
 *
 */
int speakerCallback(const void* inputBuffer,
                     void* outputBuffer,
                     unsigned long framesPerBuffer,
                     const PaStreamCallbackTimeInfo* timeInfo,
                     PaStreamCallbackFlags statusFlags,
                     void* userData) {

    if (!userData || !outputBuffer) {
        return paAbort;
    }

    audio::OutputStreamContext* ctx = static_cast<audio::OutputStreamContext*>(userData);

    int16_t* output = static_cast<int16_t*>(outputBuffer);

    int total_samples = framesPerBuffer * ctx->info.num_channels;

    // Load current playback position from the context
    uint64_t read_pos = ctx->playback_position.load(std::memory_order_relaxed);

    // Read samples from our circular buffer and put into portaudio output buffer
    int samples_read = ctx->read_samples(output, total_samples, read_pos);

    // Store updated playback position
    ctx->playback_position.store(read_pos, std::memory_order_relaxed);

    // If we didn't get enough samples, fill the rest with silence
    for (int i = samples_read; i < total_samples; i++) {
        output[i] = 0;
    }

    return paContinue;
}


std::vector<std::string> Speaker::validate(vsdk::ResourceConfig cfg) {
    auto attrs = cfg.attributes();

    if(attrs.count("device_name")) {
        if (!attrs["device_name"].is_a<std::string>()) {
            VIAM_SDK_LOG(error) << "[validate] device_name attribute must be a string";
            throw std::invalid_argument("device_name attribute must be a string");
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

viam::sdk::ProtoStruct Speaker::do_command(const viam::sdk::ProtoStruct& command) {
    throw std::runtime_error("do_command is unimplemented");
}


/**
 * Play audio data through the speaker.
 *
 * This method blocks until the audio has been completely played back.
 * Audio is written to an internal circular buffer and played asynchronously
 * by the PortAudio callback. The method waits until playback is complete
 * before returning.
 *
 * @throws std::invalid_argument if codec is not PCM_16 or data size is invalid
 */
void Speaker::play(std::vector<uint8_t> const& audio_data,
                      boost::optional<viam::sdk::audio_info> info,
                      const viam::sdk::ProtoStruct& extra) {

    std::lock_guard<std::mutex> playback_lock(playback_mu_);

    VIAM_SDK_LOG(info) << "Play called, adding samples to playback buffer";

    if (info && info->codec != vsdk::audio_codecs::PCM_16) {
        VIAM_SDK_LOG(error) << "unsupported codec: " << info->codec << " only PCM_16 is supported";
        throw std::invalid_argument("Audio codec must be PCM16 format");
    }

    // Convert uint8_t bytes to int16_t samples
    // PCM_16 means each sample is 2 bytes (16 bits)
    if (audio_data.size() % 2 != 0) {
        VIAM_SDK_LOG(error) << "Audio data size must be even for PCM_16 format, got "
                           << audio_data.size() << " bytes";
        throw std::invalid_argument("Audio data size must be even for PCM16 format");
    }

    const int16_t* samples = reinterpret_cast<const int16_t*>(audio_data.data());
    size_t num_samples = audio_data.size() / sizeof(int16_t);

    VIAM_SDK_LOG(debug) << "Playing " << num_samples << " samples ("
                       << audio_data.size() << " bytes)";

    // Write samples to the audio buffer
      uint64_t start_position;
      {
          std::lock_guard<std::mutex> lock(stream_mu_);
          start_position = audio_context_->get_write_position();

          for (size_t i = 0; i < num_samples; i++) {
              audio_context_->write_sample(samples[i]);
          }
      }

      uint64_t end_position = start_position + num_samples;

      // Block until playback position catches up
      VIAM_SDK_LOG(debug) << "Waiting for playback to complete...";
      while (audio_context_->playback_position.load() - start_position < num_samples) {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }

      VIAM_SDK_LOG(info) << "Audio playback complete";
}

viam::sdk::audio_properties Speaker::get_properties(const vsdk::ProtoStruct& extra) {
    viam::sdk::audio_properties props;

    props.supported_codecs = {
        vsdk::audio_codecs::PCM_16
    };
    std::lock_guard<std::mutex> lock(stream_mu_);
    props.sample_rate_hz = sample_rate_;
    props.num_channels = num_channels_;

    return props;

}

std::vector<viam::sdk::GeometryConfig> Speaker::get_geometries(const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("get_geometries is unimplemented");
}

void Speaker::reconfigure(const vsdk::Dependencies& deps,
                          const vsdk::ResourceConfig& cfg) {

    VIAM_SDK_LOG(info) << "[reconfigure] Speaker reconfigure start";

    try {
        auto cfg_params = audio::utils::parseConfigAttributes(cfg);

        auto params = audio::utils::setupStreamFromConfig(
            cfg_params,
            audio::utils::StreamDirection::Output,
            speakerCallback,
            pa_
        );

        // Create audio context with actual sample rate/channels from params
        vsdk::audio_info info{vsdk::audio_codecs::PCM_16, params.sample_rate, params.num_channels};
        int samples_per_chunk = params.sample_rate * audio::CHUNK_DURATION_SECONDS;
        auto new_audio_context = std::make_shared<audio::OutputStreamContext>(info, samples_per_chunk);

        // Set user_data to point to the audio context
        params.user_data = new_audio_context.get();

        // Check if there's unplayed audio
        if (audio_context_) {
            uint64_t write_pos = audio_context_->get_write_position();
            uint64_t playback_pos = audio_context_->playback_position.load();
            if (write_pos > playback_pos) {
                uint64_t unplayed_samples = write_pos - playback_pos;
                double unplayed_seconds = static_cast<double>(unplayed_samples) /
                                        (audio_context_->info.sample_rate_hz * audio_context_->info.num_channels);
                VIAM_SDK_LOG(warn) << "[reconfigure] Discarding " << unplayed_seconds
                                    << " seconds of unplayed audio";
            }
    }

        // Set new configuration and restart stream under lock
        {
            std::lock_guard<std::mutex> lock(stream_mu_);
            device_name_ = params.device_name;
            sample_rate_ = params.sample_rate;
            num_channels_ = params.num_channels;
            latency_ = params.latency_seconds;
            audio_context_ = new_audio_context;

            audio::utils::restart_stream(stream_, params, pa_);
        }
        VIAM_SDK_LOG(info) << "[reconfigure] Reconfigure completed successfully";
    } catch (const std::exception& e) {
        VIAM_SDK_LOG(error) << "[reconfigure] Reconfigure failed: " << e.what();
        throw;
    }

}


} // namespace speaker
