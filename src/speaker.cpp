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

// Distance from the buffer's lap point at which process_and_write_pcm blocks waiting for
// the callback to drain. This is what paces a faster-than-real-time producer (TTS, file
// playback) — and the size of the margin is the cushion against any delay in the callback
// advancing playback_position (scheduler jitter, USB stalls, etc.).
constexpr int BUFFER_MARGIN_MS = 50;

Speaker::Speaker(viam::sdk::Dependencies deps, viam::sdk::ResourceConfig cfg, audio::portaudio::PortAudioInterface* pa)
    : viam::sdk::AudioOut(cfg.name()), pa_(pa), stream_(nullptr) {
    auto setup = audio::utils::setup_audio_device<audio::OutputStreamContext>(
        cfg, audio::utils::StreamDirection::Output, speakerCallback, pa_, audio::BUFFER_DURATION_SECONDS);

    // Set new configuration and start stream under lock
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        audio_context_ = setup.audio_context;
        setup.stream_params.user_data = setup.audio_context.get();
        stream_params_ = setup.stream_params;
        device_id_ = setup.config_params.device_id;
        audio::utils::restart_stream(stream_, stream_params_, pa_);
        latency_ = audio::utils::get_stream_latency(stream_, stream_params_, pa_);
        volume_ = setup.config_params.volume;
        if (volume_) {
            audio::volume::set_volume(stream_params_.device_name, *volume_);
        }
    }

    watchdog_ = std::make_unique<audio::utils::StallWatchdog<audio::OutputStreamContext>>(
        [this]() {
            std::lock_guard<std::mutex> lock(stream_mu_);
            return audio_context_;
        },
        [this]() {
            std::lock_guard<std::mutex> lock(stream_mu_);
            return restart_attempts_;
        },
        [this](const std::shared_ptr<audio::OutputStreamContext>& ctx) { restart_stalled_stream(ctx); },
        "[speaker stall_watcher]");
    watchdog_->start();
}

Speaker::~Speaker() {
    // Stop and join the watchdog before tearing down the stream so it can't touch a
    // half-destroyed audio_context_.
    if (watchdog_) {
        watchdog_->stop();
    }

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
 * Tear down the existing stream and bring up a fresh one with the saved params.
 *
 * Bails out if `playback_context` is no longer the active audio_context_ — that means
 * another stall recovery already replaced it, so the in-flight play() call (if any)
 * is about to exit on its own and we shouldn't race.
 *
 * On a successful restart, any unplayed audio in the old buffer is discarded. The
 * in-flight play() loop will see audio_context_ change and return early.
 */
void Speaker::restart_stalled_stream(const std::shared_ptr<audio::OutputStreamContext>& playback_context) {
    std::lock_guard<std::mutex> lock(stream_mu_);
    if (playback_context != audio_context_) {
        return;
    }

    VIAM_SDK_LOG(debug) << "[speaker stall_watcher] Restarting stalled speaker stream";

    // If device_id was configured, re-resolve before restarting in case the kernel
    // re-enumerated and the cached device_index is stale (e.g. USB unplug/replug).
    // When the device is missing, skip the actual stream open — PortAudio would just
    // spam ALSA errors. Bump attempts so the watchdog enters backoff; once the device
    // returns, a backoff retry will resolve and proceed.
    if (!audio::utils::resolve_device_id_into_params(device_id_, stream_params_, pa_, "[speaker stall_watcher]")) {
        if (restart_attempts_ < audio::utils::MAX_RESTART_ATTEMPTS) {
            ++restart_attempts_;
        }
        return;
    }

    if (stream_) {
        try {
            audio::utils::abort_stream(stream_, pa_);
        } catch (const std::exception& e) {
            VIAM_SDK_LOG(error) << "[speaker stall_watcher] Error shutting down stalled stream: " << e.what();
        }
        stream_ = nullptr;
    }

    const viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, stream_params_.sample_rate, stream_params_.num_channels};
    const auto new_context = std::make_shared<audio::OutputStreamContext>(info, audio::BUFFER_DURATION_SECONDS);

    try {
        stream_params_.user_data = new_context.get();
        audio::utils::restart_stream(stream_, stream_params_, pa_);
        latency_ = audio::utils::get_stream_latency(stream_, stream_params_, pa_);
        audio_context_ = new_context;
        restart_attempts_ = 0;
        VIAM_SDK_LOG(info) << "[speaker stall_watcher] Speaker stream restarted successfully";
    } catch (const std::exception& e) {
        if (restart_attempts_ < audio::utils::MAX_RESTART_ATTEMPTS) {
            ++restart_attempts_;
        }
        VIAM_SDK_LOG(error) << "[speaker stall_watcher] Failed to restart stream (attempt " << restart_attempts_ << "/"
                            << audio::utils::MAX_RESTART_ATTEMPTS << "): " << e.what();
    }
}

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

    ctx->last_callback_time_ns.store(static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()));

    if (statusFlags & paOutputOverflow) {
        ctx->output_overflow_count.fetch_add(1);
    }
    if (statusFlags & paOutputUnderflow) {
        ctx->output_underflow_count.fetch_add(1);
    }

    int16_t* const output = static_cast<int16_t*>(outputBuffer);

    const uint64_t total_samples = framesPerBuffer * ctx->info.num_channels;

    // Load current playback position from the context
    uint64_t read_pos = ctx->playback_position.load();

    // Read samples from our circular buffer and put into portaudio output buffer
    const int samples_read = ctx->read_samples(output, total_samples, read_pos);

    // Store updated playback position
    ctx->playback_position.store(read_pos);

    // If we didn't get enough samples, fill the rest with silence
    for (int i = samples_read; i < total_samples; i++) {
        output[i] = 0;
    }

    return paContinue;
}

std::vector<std::string> Speaker::validate(vsdk::ResourceConfig cfg) {
    auto attrs = cfg.attributes();

    if (attrs.count("device_id")) {
        if (!attrs["device_id"].is_a<std::string>()) {
            VIAM_SDK_LOG(error) << "[validate] device_id attribute must be a string";
            throw std::invalid_argument("device_id attribute must be a string");
        }
    }
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
        audio::volume::set_volume(stream_params_.device_name, vol);
        volume_ = vol;

        return viam::sdk::ProtoStruct{{"volume", static_cast<double>(vol)}};
    }

    if (command.count("stop")) {
        VIAM_SDK_LOG(info) << "Stop command received, interrupting playback";
        stop_requested_.store(true);
        // Advance playback position to write position so no more audio is played.
        std::lock_guard<std::mutex> lock(stream_mu_);
        if (audio_context_) {
            audio_context_->playback_position.store(audio_context_->get_write_position());
        }
        return viam::sdk::ProtoStruct{{"stopped", true}};
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
    stop_requested_.store(false);

    VIAM_SDK_LOG(debug) << "Play called, adding samples to playback buffer";

    if (audio_data.empty()) {
        return;
    }

    if (!info) {
        VIAM_SDK_LOG(error) << "[Play]: Must specify audio info parameter";
        throw std::invalid_argument("[Play]: Must specify audio info parameter");
    }

    const std::string codec_str = info->codec;
    AudioCodec codec = audio::codec::parse_codec(codec_str);

    // Detect and strip WAV header if present
    const uint8_t* raw_audio = audio_data.data();
    size_t raw_audio_size = audio_data.size();
    int audio_sample_rate = info->sample_rate_hz;
    int audio_num_channels = info->num_channels;
    if (audio::codec::has_wav_header(raw_audio, raw_audio_size)) {
        audio_num_channels = audio::codec::wav_num_channels(raw_audio);
        audio_sample_rate = audio::codec::wav_sample_rate(raw_audio);
        VIAM_SDK_LOG(debug) << "[play] Detected WAV header (" << audio_sample_rate << "Hz, " << audio_num_channels << "ch), stripping "
                            << audio::codec::wav_header_size << "-byte header";
        raw_audio += audio::codec::wav_header_size;
        raw_audio_size -= audio::codec::wav_header_size;
    }

    // MP3 needs a stateful whole-buffer decode; the rest of the pipeline only sees PCM_16.
    std::vector<uint8_t> mp3_decoded;
    if (codec == AudioCodec::MP3) {
        MP3DecoderContext mp3_ctx;
        decode_mp3_to_pcm16(mp3_ctx, raw_audio, raw_audio_size, mp3_decoded);
        audio_sample_rate = mp3_ctx.sample_rate;
        audio_num_channels = mp3_ctx.num_channels;
        raw_audio = mp3_decoded.data();
        raw_audio_size = mp3_decoded.size();
        codec = AudioCodec::PCM_16;
    }

    int speaker_sample_rate;
    int speaker_num_channels;
    std::shared_ptr<audio::OutputStreamContext> playback_context;
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        if (!audio_context_) {
            VIAM_SDK_LOG(error) << "[Play] Audio context is nullptr";
            throw std::runtime_error("Audio context is nullptr");
        }
        playback_context = audio_context_;
        speaker_sample_rate = stream_params_.sample_rate;
        speaker_num_channels = stream_params_.num_channels;
    }

    // Estimate post-resample sample count from input bytes — exact for PCM_16, ~2x conservative
    // for PCM_32 variants — to bail before allocating if the audio won't fit the playback buffer.
    {
        const size_t pcm16_bytes_estimate = (codec == AudioCodec::PCM_16) ? raw_audio_size : raw_audio_size / 2;
        const size_t input_samples_estimate = pcm16_bytes_estimate / sizeof(int16_t);
        const double duration_seconds = static_cast<double>(input_samples_estimate) / (audio_sample_rate * audio_num_channels);
        if (duration_seconds > audio::BUFFER_DURATION_SECONDS) {
            throw std::invalid_argument("Audio file too long for playback buffer (max " + std::to_string(audio::BUFFER_DURATION_SECONDS) +
                                        " seconds); use PlayStream for longer audio");
        }
    }

    const uint64_t start_position = playback_context->get_write_position();
    const size_t samples_written = process_and_write_pcm(raw_audio,
                                                         raw_audio_size,
                                                         codec,
                                                         audio_sample_rate,
                                                         audio_num_channels,
                                                         speaker_sample_rate,
                                                         speaker_num_channels,
                                                         playback_context);

    wait_for_playback(playback_context, start_position, samples_written);
}

size_t Speaker::process_and_write_pcm(const uint8_t* data,
                                      size_t size,
                                      AudioCodec codec,
                                      int audio_sample_rate,
                                      int audio_num_channels,
                                      int speaker_sample_rate,
                                      int speaker_num_channels,
                                      std::shared_ptr<audio::OutputStreamContext> playback_context) {
    // A 0 return is reserved to mean "the stream context was swapped out"; reject empty
    // input up front so callers can rely on that contract.
    if (size == 0) {
        throw std::invalid_argument("process_and_write_pcm: empty input");
    }

    std::vector<uint8_t> decoded_buf;
    const uint8_t* decoded_data = nullptr;
    size_t decoded_size = 0;
    switch (codec) {
        case AudioCodec::PCM_32:
            audio::codec::convert_pcm32_to_pcm16(data, size, decoded_buf);
            decoded_data = decoded_buf.data();
            decoded_size = decoded_buf.size();
            break;
        case AudioCodec::PCM_32_FLOAT:
            audio::codec::convert_float32_to_pcm16(data, size, decoded_buf);
            decoded_data = decoded_buf.data();
            decoded_size = decoded_buf.size();
            break;
        case AudioCodec::PCM_16:
            decoded_data = data;
            decoded_size = size;
            break;
        default:
            throw std::invalid_argument("process_and_write_pcm: unsupported codec");
    }

    if (decoded_size % 2 != 0) {
        throw std::invalid_argument("process_and_write_pcm: PCM_16 size must be even, got " + std::to_string(decoded_size));
    }

    const int16_t* samples = reinterpret_cast<const int16_t*>(decoded_data);
    size_t num_samples = decoded_size / sizeof(int16_t);

    std::vector<int16_t> channel_converted;
    if (audio_num_channels != speaker_num_channels) {
        convert_channels(samples, num_samples, audio_num_channels, speaker_num_channels, channel_converted);
        samples = channel_converted.data();
        num_samples = channel_converted.size();
    }

    std::vector<int16_t> resampled;
    if (audio_sample_rate != speaker_sample_rate) {
        resample_audio(audio_sample_rate, speaker_sample_rate, speaker_num_channels, samples, num_samples, resampled);
        samples = resampled.data();
        num_samples = resampled.size();
    }

    // Guarantees the post-decode pipeline preserves the "non-empty in → non-empty out" invariant,
    // so the 0 return below can only mean a context swap.
    if (num_samples == 0) {
        throw std::invalid_argument("process_and_write_pcm: input too small to produce any output samples after resample");
    }

    // Backpressure: cap how far the producer can run ahead of the callback so a faster-than-
    // real-time source can't lap the read pointer and erase audio.
    const uint64_t margin_samples =
        static_cast<uint64_t>(speaker_sample_rate) * speaker_num_channels * BUFFER_MARGIN_MS / 1000;
    const uint64_t max_ahead = static_cast<uint64_t>(playback_context->buffer_capacity) - margin_samples;

    for (size_t i = 0; i < num_samples; ++i) {
        while (playback_context->get_write_position() - playback_context->playback_position.load() >= max_ahead) {
            if (stop_requested_.load()) {
                return i;
            }
            {
                std::lock_guard<std::mutex> lock(stream_mu_);
                if (audio_context_ != playback_context) {
                    return 0;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        playback_context->write_sample(samples[i]);
    }
    return num_samples;
}

void Speaker::wait_for_playback(std::shared_ptr<audio::OutputStreamContext> playback_context,
                                uint64_t start_position,
                                uint64_t samples_to_drain) {
    uint64_t last_logged_overflow_count = 0;
    uint64_t last_logged_underflow_count = 0;
    uint64_t last_staleness_log_ns = 0;
    while (playback_context->playback_position.load() - start_position < samples_to_drain) {
        if (stop_requested_.load()) {
            VIAM_SDK_LOG(debug) << "Playback stopped by stop command";
            return;
        }
        PaStream* current_stream = nullptr;
        {
            std::lock_guard<std::mutex> lock(stream_mu_);
            if (audio_context_ != playback_context) {
                VIAM_SDK_LOG(debug) << "Audio playback interrupted by stream restart, exiting";
                return;
            }
            current_stream = stream_;
        }

        audio::utils::log_callback_staleness(playback_context->last_callback_time_ns, "[playback]", current_stream, last_staleness_log_ns);

        const uint64_t overflow_count = playback_context->output_overflow_count.load();
        if (overflow_count != last_logged_overflow_count) {
            VIAM_SDK_LOG(warn) << "[playback] Output overflow detected — " << (overflow_count - last_logged_overflow_count)
                               << " new overflow(s), " << overflow_count << " total";
            last_logged_overflow_count = overflow_count;
        }

        const uint64_t underflow_count = playback_context->output_underflow_count.load();
        if (underflow_count != last_logged_underflow_count) {
            VIAM_SDK_LOG(warn) << "[playback] Output underflow detected — " << (underflow_count - last_logged_underflow_count)
                               << " new underflow(s), " << underflow_count << " total";
            last_logged_underflow_count = underflow_count;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Drain the PortAudio pipeline so the caller knows the audio actually played. Skipped on
    // the early-return paths above (stop / context swap) — both want to exit promptly.
    double drain_latency;
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        drain_latency = latency_;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(drain_latency * 1000)));
}

// Resampling and channel conversion run per-chunk against the source's audio_info, so chunk
// boundaries that don't align with the resampler's window can introduce minor artifacts.
void Speaker::play_stream(viam::sdk::audio_info info,
                          std::function<boost::optional<std::vector<uint8_t>>()> chunk_source,
                          const viam::sdk::ProtoStruct& extra) {
    std::lock_guard<std::mutex> playback_lock(playback_mu_);
    stop_requested_.store(false);

    const AudioCodec source_codec = audio::codec::parse_codec(info.codec);
    if (source_codec == AudioCodec::MP3) {
        throw std::invalid_argument("[PlayStream] MP3 streaming is not supported; use play() for MP3");
    }

    int speaker_sample_rate;
    int speaker_num_channels;
    std::shared_ptr<audio::OutputStreamContext> playback_context;
    {
        std::lock_guard<std::mutex> lock(stream_mu_);
        if (!audio_context_) {
            throw std::runtime_error("[PlayStream] Audio context is nullptr");
        }
        playback_context = audio_context_;
        speaker_sample_rate = stream_params_.sample_rate;
        speaker_num_channels = stream_params_.num_channels;
    }

    const uint64_t start_position = playback_context->get_write_position();
    uint64_t total_samples_written = 0;

    while (auto chunk = chunk_source()) {
        if (stop_requested_.load()) {
            break;
        }
        if (chunk->empty()) {
            continue;
        }

        const size_t written = process_and_write_pcm(chunk->data(),
                                                     chunk->size(),
                                                     source_codec,
                                                     info.sample_rate_hz,
                                                     info.num_channels,
                                                     speaker_sample_rate,
                                                     speaker_num_channels,
                                                     playback_context);
        if (written == 0) {
            // Stream context was swapped; bail without draining.
            return;
        }
        total_samples_written += written;
    }

    wait_for_playback(playback_context, start_position, total_samples_written);
}

viam::sdk::audio_properties Speaker::get_properties(const vsdk::ProtoStruct& extra) {
    viam::sdk::audio_properties props;

    props.supported_codecs = {
        vsdk::audio_codecs::PCM_16, vsdk::audio_codecs::PCM_32, vsdk::audio_codecs::PCM_32_FLOAT, vsdk::audio_codecs::MP3};
    std::lock_guard<std::mutex> lock(stream_mu_);
    props.sample_rate_hz = stream_params_.sample_rate;
    props.num_channels = stream_params_.num_channels;

    return props;
}

std::vector<viam::sdk::GeometryConfig> Speaker::get_geometries(const viam::sdk::ProtoStruct& extra) {
    throw std::runtime_error("get_geometries is unimplemented");
}

}  // namespace speaker
