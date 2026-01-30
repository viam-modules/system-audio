#include "speaker.hpp"
#include <cstring>
#include <stdexcept>
#include <thread>
#include <viam/sdk/common/exception.hpp>
#include <viam/sdk/components/audio_out.hpp>
#include <viam/sdk/registry/registry.hpp>
#include "audio_buffer.hpp"
#include "audio_codec.hpp"
#include "audio_utils.hpp"
#include "mp3_decoder.hpp"
#include "resample.hpp"
#include "volume.hpp"

namespace speaker {
namespace vsdk = ::viam::sdk;
using audio::codec::AudioCodec;

constexpr int MIN_VOLUME = 0;
constexpr int MAX_VOLUME = 100;

Speaker::Speaker(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg, audio::portaudio::PortAudioInterface* pa)
    : viam::sdk::AudioOut(cfg.name()), pa_(pa), stream_(nullptr) {
    auto setup = audio::utils::setup_audio_device<audio::OutputStreamContext>(
        cfg, audio::utils::StreamDirection::Output, speakerCallback, pa_, audio::BUFFER_DURATION_SECONDS);

    // Set new configuration and start stream under lock
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        device_name_ = setup.stream_params.device_name;
        sample_rate_ = setup.stream_params.sample_rate;
        num_channels_ = setup.stream_params.num_channels;
        latency_ = setup.stream_params.latency_seconds;
        audio_context_ = setup.audio_context;
        audio::utils::restart_stream(stream_, setup.stream_params, pa_);
        volume_ = setup.config_params.volume;
        if (volume_) {
            audio::volume::set_volume(device_name_, *volume_);
        }
    }
}

Speaker::~Speaker() {
    if (stream_) {
        PaError err = Pa_StopStream(stream_);
        if (err != paNoError) {
            VIAM_SDK_LOG(error) << "Failed to stop stream in destructor: " << Pa_GetErrorText(err);
        }

        err = Pa_CloseStream(stream_);
        if (err != paNoError) {
            VIAM_SDK_LOG(error) << "Failed to close stream in destructor: " << Pa_GetErrorText(err);
        }
    }
}

vsdk::Model Speaker::model = {"viam", "system-audio", "speaker"};

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
                    const unsigned long framesPerBuffer,
                    const PaStreamCallbackTimeInfo* timeInfo,
                    PaStreamCallbackFlags statusFlags,
                    void* userData) {
    if (!userData || !outputBuffer) {
        return paAbort;
    }

    audio::OutputStreamContext* const ctx = static_cast<audio::OutputStreamContext*>(userData);

    int16_t* const output = static_cast<int16_t*>(outputBuffer);

    const uint64_t total_samples = framesPerBuffer * ctx->info.num_channels;

    // Load current playback position from the context
    uint64_t read_pos = ctx->playback_position.load(std::memory_order_relaxed);

    // Read samples from our circular buffer and put into portaudio output buffer
    const int samples_read = ctx->read_samples(output, total_samples, read_pos);

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

    if (attrs.count("device_name")) {
        if (!attrs["device_name"].is_a<std::string>()) {
            VIAM_SDK_LOG(error) << "[validate] device_name attribute must be a string";
            throw std::invalid_argument("device_name attribute must be a string");
        }
    }
    if (attrs.count("latency")) {
        if (!attrs["latency"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] latency attribute must be a number";
            throw std::invalid_argument("latency attribute must be a number");
        }
        const double latency_ms = *attrs.at("latency").get<double>();
        if (latency_ms < 0) {
            VIAM_SDK_LOG(error) << "[validate] latency must be non-negative";
            throw std::invalid_argument("latency must be non-negative");
        }
    }
    if (attrs.count("sample_rate")) {
        if (!attrs["sample_rate"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] sample_rate attribute must be a number";
            throw std::invalid_argument("sample_rate attribute must be a number");
        }
        double sample_rate = *attrs.at("sample_rate").get<double>();
        if (sample_rate <= 0) {
            VIAM_SDK_LOG(error) << "[validate] sample rate must be greater than zero";
            throw std::invalid_argument("sample rate must be greater than zero");
        }
    }
    if (attrs.count("num_channels")) {
        if (!attrs["num_channels"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] num_channels attribute must be a number";
            throw std::invalid_argument("num_channels attribute must be a number");
        }
        double num_channels = *attrs.at("num_channels").get<double>();
        if (num_channels <= 0) {
            VIAM_SDK_LOG(error) << "[validate] num_channels must be greater than zero";
            throw std::invalid_argument(" num_channels must be greater than zero");
        }
    }
    if (attrs.count("volume")) {
        if (!attrs["volume"].is_a<double>()) {
            VIAM_SDK_LOG(error) << "[validate] volume attribute must be a number";
            throw std::invalid_argument("volume attribute must be a number");
        }
        double vol = *attrs.at("volume").get<double>();
        if (vol < MIN_VOLUME || vol > MAX_VOLUME) {
            VIAM_SDK_LOG(error) << "[validate] volume must be between 0 and 100";
            throw std::invalid_argument("volume must be between 0 and 100");
        }
    }

    return {};
}

viam::sdk::ProtoStruct Speaker::do_command(const viam::sdk::ProtoStruct& command) {
    if (command.count("set_volume")) {
        if (!command.at("set_volume").is_a<double>()) {
            throw std::invalid_argument("set_volume must be a number");
        }
        int vol = static_cast<int>(*command.at("set_volume").get<double>());
        if (vol < MIN_VOLUME || vol > MAX_VOLUME) {
            throw std::invalid_argument("volume must be between 0 and 100");
        }

        std::lock_guard<std::mutex> lock(stream_mu_);
        audio::volume::set_volume(device_name_, vol);
        volume_ = vol;

        return viam::sdk::ProtoStruct{{"volume", static_cast<double>(vol)}};
    }

    throw std::invalid_argument("unknown command");
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

    VIAM_SDK_LOG(debug) << "Play called, adding samples to playback buffer";

    if (!info) {
        VIAM_SDK_LOG(error) << "[Play]: Must specify audio info parameter";
        throw std::invalid_argument("[Play]: Must specify audio info parameter");
    }

    const std::string codec_str = info->codec;

    // Parse codec string to enum
    const AudioCodec codec = audio::codec::parse_codec(codec_str);

    std::vector<uint8_t> decoded_data;
    int audio_sample_rate = info->sample_rate_hz;
    int audio_num_channels = info->num_channels;

    // decode to pcm16
    switch (codec) {
        case AudioCodec::MP3: {
            MP3DecoderContext mp3_ctx;
            decode_mp3_to_pcm16(mp3_ctx, audio_data, decoded_data);
            // For MP3, use the decoded properties from the file, not what user provided
            audio_sample_rate = mp3_ctx.sample_rate;
            audio_num_channels = mp3_ctx.num_channels;
            break;
        }
        case AudioCodec::PCM_32:
            audio::codec::convert_pcm32_to_pcm16(audio_data.data(), audio_data.size(), decoded_data);
            break;
        case AudioCodec::PCM_32_FLOAT:
            audio::codec::convert_float32_to_pcm16(audio_data.data(), audio_data.size(), decoded_data);
            break;
        case AudioCodec::PCM_16:
            decoded_data = audio_data;
            break;
        default:
            // Shouldn't ever get here because it will throw when converting the str to enum,
            // but for safety
            VIAM_SDK_LOG(error) << "Unsupported codec for playback: " << codec_str;
            throw std::invalid_argument("Unsupported codec for playback");
    }

    // Convert uint8_t bytes to int16_t samples
    // PCM_16 means each sample is 2 bytes (16 bits)
    if (decoded_data.size() % 2 != 0) {
        VIAM_SDK_LOG(error) << "Audio data size must be even for PCM_16 format, got " << decoded_data.size() << " bytes";
        throw std::invalid_argument("got invalid data size, cannot convert to int16");
    }

    const int16_t* decoded_samples = reinterpret_cast<const int16_t*>(decoded_data.data());
    size_t num_samples = decoded_data.size() / sizeof(int16_t);

    int speaker_sample_rate;

    // Validate decoded audio properties match speaker configuration
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        speaker_sample_rate = sample_rate_;
        if (audio_num_channels != num_channels_) {
            VIAM_SDK_LOG(error) << "Channel mismatch: speaker=" << num_channels_ << " channels, decoded audio=" << audio_num_channels
                                << " channels";
            throw std::invalid_argument("Channel mismatch: speaker=" + std::to_string(num_channels_) +
                                        " channels, decoded audio=" + std::to_string(audio_num_channels) + " channels");
        }
    }

    // Resample if sample rates don't match
    std::vector<int16_t> resampled_samples;
    const int16_t* samples = decoded_samples;
    size_t final_num_samples = num_samples;
    int final_sample_rate = audio_sample_rate;

    if (audio_sample_rate != speaker_sample_rate) {
        VIAM_SDK_LOG(info) << "resampling audio from " << audio_sample_rate << "Hz to speaker native sample rate " << speaker_sample_rate
                           << " Hz";
        resample_audio(audio_sample_rate, speaker_sample_rate, audio_num_channels, decoded_samples, num_samples, resampled_samples);

        // Use resampled data
        samples = resampled_samples.data();
        final_num_samples = resampled_samples.size();
        final_sample_rate = speaker_sample_rate;
    }

    // Check if audio duration exceeds playback buffer capacity
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        double duration_seconds = static_cast<double>(final_num_samples) / (final_sample_rate * audio_num_channels);
        if (duration_seconds > audio::BUFFER_DURATION_SECONDS) {
            VIAM_SDK_LOG(error) << "Audio duration (" << duration_seconds << " seconds) exceeds maximum playback buffer size ("
                                << audio::BUFFER_DURATION_SECONDS << " seconds)";
            throw std::invalid_argument("Audio file too long for playback buffer (max " + std::to_string(audio::BUFFER_DURATION_SECONDS) +
                                        " seconds)");
        }
    }

    VIAM_SDK_LOG(debug) << "Playing " << final_num_samples << " samples (" << final_num_samples * sizeof(int16_t) << " bytes)";

    // Write samples to the audio buffer and capture context
    uint64_t start_position;
    std::shared_ptr<audio::OutputStreamContext> playback_context;
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        if (!audio_context_) {
            VIAM_SDK_LOG(error) << "[Play] Audio context is nullptr";
            throw std::runtime_error("Audio context is nullptr");
        }
        playback_context = audio_context_;
        start_position = audio_context_->get_write_position();

        for (size_t i = 0; i < final_num_samples; i++) {
            audio_context_->write_sample(samples[i]);
        }
    }

    // Block until playback position catches up
    VIAM_SDK_LOG(debug) << "Waiting for playback to complete...";
    while (playback_context->playback_position.load() - start_position < final_num_samples) {
        // Check if context changed (reconfigure happened)
        {
            std::lock_guard<std::mutex> lock(stream_mu_);
            if (audio_context_ != playback_context) {
                VIAM_SDK_LOG(debug) << "Audio playback interrupted by reconfigure, exiting";
                return;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Wait for audio pipeline to drain
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(latency_ * 1000)));

    VIAM_SDK_LOG(debug) << "Audio playback complete";
}

viam::sdk::audio_properties Speaker::get_properties(const vsdk::ProtoStruct& extra) {
    viam::sdk::audio_properties props;

    props.supported_codecs = {
        vsdk::audio_codecs::PCM_16, vsdk::audio_codecs::PCM_32, vsdk::audio_codecs::PCM_32_FLOAT, vsdk::audio_codecs::MP3};
    std::lock_guard<std::mutex> lock(stream_mu_);
    props.sample_rate_hz = sample_rate_;
    props.num_channels = num_channels_;

    return props;
}

std::vector<viam::sdk::GeometryConfig> Speaker::get_geometries(const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("get_geometries is unimplemented");
}

void Speaker::reconfigure(const vsdk::Dependencies& deps, const vsdk::ResourceConfig& cfg) {
    VIAM_SDK_LOG(info) << "[reconfigure] Speaker reconfigure start";

    try {
        // Check if there's unplayed audio before reconfiguring
        {
            std::lock_guard<std::mutex> lock(stream_mu_);
            if (audio_context_) {
                const uint64_t write_pos = audio_context_->get_write_position();
                const uint64_t playback_pos = audio_context_->playback_position.load();

                if (write_pos > playback_pos) {
                    const uint64_t unplayed_samples = write_pos - playback_pos;
                    const double unplayed_seconds =
                        static_cast<double>(unplayed_samples) / (audio_context_->info.sample_rate_hz * audio_context_->info.num_channels);
                    VIAM_SDK_LOG(warn) << "[reconfigure] Discarding " << unplayed_seconds << " seconds of unplayed audio";
                }
            }
        }

        auto setup = audio::utils::setup_audio_device<audio::OutputStreamContext>(
            cfg, audio::utils::StreamDirection::Output, speakerCallback, pa_, audio::BUFFER_DURATION_SECONDS);

        // Set new configuration and restart stream under lock
        {
            std::lock_guard<std::mutex> lock(stream_mu_);

            // Stop the stream first efore replacing audio_context_
            // Otherwise the callback thread may still be accessing the old context
            // after we destroy it (heap-use-after-free)
            audio::utils::restart_stream(stream_, setup.stream_params, pa_);
            device_name_ = setup.stream_params.device_name;
            sample_rate_ = setup.stream_params.sample_rate;
            num_channels_ = setup.stream_params.num_channels;
            latency_ = setup.stream_params.latency_seconds;
            audio_context_ = setup.audio_context;
            volume_ = setup.config_params.volume;
            if (volume_) {
                audio::volume::set_volume(device_name_, *volume_);
            }
        }
        VIAM_SDK_LOG(info) << "[reconfigure] Reconfigure completed successfully";
    } catch (const std::exception& e) {
        VIAM_SDK_LOG(error) << "[reconfigure] Reconfigure failed: " << e.what();
        throw;
    }
}

}  // namespace speaker
