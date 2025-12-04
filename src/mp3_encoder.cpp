#include <cstring>
#include <stdexcept>
#include "mp3_encoder.hpp"

namespace microphone {


// Helper function to convert MP3LAME initialization error codes to readable strings
static const std::string mp3lame_init_error_to_string(int error_code) {
    switch (error_code) {
        case LAME_GENERICERROR:
            return "MP3LAME: generic error";
        case LAME_NOMEM:
            return "MP3LAME: no memory error: out of memory";
        case LAME_BADBITRATE:
            return "MP3LAME: invalid bit rate";
        case LAME_BADSAMPFREQ:
            return "MP3LAME: invalid sample rate";
        case LAME_INTERNALERROR:
            return "MP3LAME internal error";
        default:
            return "Unknown MP3LAME initialization error";
    }
}

// Helper function to convert MP3LAME encoding error codes to readable strings
static const std::string mp3lame_encode_error_to_string(int error_code) {
    switch (error_code) {
        case -1:
            return "MP3LAME: mp3buf is too small";
        case -2:
            return "MP3LAME: malloc() problem";
        case -3:
            return "MP3LAME: lame_init_params() not called";
        case -4:
            return "MP3LAME: psycho acoustic problems";
        default:
            return "Unknown MP3LAME encoding error";
    }
}

void initialize_mp3_encoder(MP3EncoderContext& ctx, int sample_rate, int num_channels) {
    ctx.sample_rate = sample_rate;
    ctx.num_channels = num_channels;

    CleanupPtr<lame_close> lame(lame_init());
    if (!lame) {
        VIAM_SDK_LOG(error) << "Failed to initialize MP3 encoder";
        throw std::runtime_error("Failed to initialize MP3 encoder");
    }

    ctx.encoder = std::move(lame);

    // Configure encoder
    lame_set_in_samplerate(ctx.encoder.get(), sample_rate);
    lame_set_num_channels(ctx.encoder.get(), num_channels);
    // 192 kbps bit rate - how many bits of audio used to represent one second of audio
    // higher bitrate = better quality,larger file size
    lame_set_brate(ctx.encoder.get(), MP3_BIT_RATE);
    lame_set_quality(ctx.encoder.get(), MP3_QUALITY);


    int init_result = lame_init_params(ctx.encoder.get());
    if (init_result < 0) {
        VIAM_SDK_LOG(error) << "Failed to initialize MP3 encoder parameters: "
                            << mp3lame_init_error_to_string(init_result) << " (code: " << init_result << ")";
        throw std::runtime_error("Failed to initialize MP3 encoder parameters");
    }

    // Get encoder delay - this is the number of samples (per channel) that LAME
    // buffers
    ctx.encoder_delay = lame_get_encoder_delay(ctx.encoder.get());

    // Get the actual frame size LAME is using
    ctx.frame_size = lame_get_framesize(ctx.encoder.get());


    VIAM_SDK_LOG(debug) << "MP3 encoder initialized: " << sample_rate
                       << "Hz, " << num_channels << " channels, 192kbps CBR, encoder delay: "
                       << ctx.encoder_delay << " samples, "
                       <<  " frame size: " << ctx.frame_size << " samples/frame)";
}

void encode_samples_to_mp3(MP3EncoderContext& ctx,
                               int16_t* samples,
                               int sample_count,
                               uint64_t chunk_start_position,
                               std::vector<uint8_t>& output_data) {
    if (!ctx.encoder) {
        VIAM_SDK_LOG(error) << "encode_samples_to_mp3: MP3 encoder not initialized";
        throw std::runtime_error("encode_samples_to_mp3: MP3 encoder not initialized");
    }

    if (samples == nullptr) {
        VIAM_SDK_LOG(error) << "encode_samples_to_mp3: samples pointer is null";
        throw std::invalid_argument("encode_samples_to_mp3: samples cannot be null");
    }

    if (sample_count <= 0) {
      VIAM_SDK_LOG(debug) << "encode_samples_to_mp3: no samples to encode (count=" << sample_count << ")";
      return;
    }

    int num_samples_per_channel = sample_count / ctx.num_channels;
    int mp3buf_size = static_cast<int>(1.25 * num_samples_per_channel + 7200);
    size_t current_size = output_data.size();
    output_data.resize(current_size + mp3buf_size);

    int bytes_written = 0;

    switch(ctx.num_channels) {
        case 1:
            bytes_written = lame_encode_buffer(
                ctx.encoder.get(),
                samples,              // left channel
                nullptr,              // right channel (null for mono)
                num_samples_per_channel,
                output_data.data() + current_size, // write new data starting at the end of the buffer
                mp3buf_size
            );
            break;
        case 2:
            bytes_written = lame_encode_buffer_interleaved(
                ctx.encoder.get(),
                samples,
                num_samples_per_channel,
                output_data.data() + current_size, // write new data starting at the end of the buffer
                mp3buf_size
            );
            break;
        default:
            VIAM_SDK_LOG(error) << "Unsupported number of channels: " << ctx.num_channels
            << "Only mono (1) and stereo (2) are supported";
            throw std::invalid_argument("Unsupported number of channels, only mono (1) and stereo (2) are supported");
    }

    if (bytes_written < 0) {
        VIAM_SDK_LOG(error) << "Error encoding samples: "
                            << mp3lame_encode_error_to_string(bytes_written) << " (code: " << bytes_written << ")";
        throw std::runtime_error("LAME encoding error");
    }

    // Resize to actual bytes written
    output_data.resize(current_size + bytes_written);
}

void flush_mp3_encoder(MP3EncoderContext& ctx, std::vector<uint8_t>& output_data) {
    if (!ctx.encoder) {
        VIAM_SDK_LOG(error) << "flush_mp3_encoder: encoder is null";
        throw std::invalid_argument("flush_mp3_encoder: encoder is null");
    }
    // Just flush LAME's internal lookahead buffer to get the last ~encoder_delay samples
    // from mp3lame docs: 'mp3buf' should be at least 7200 bytes long to hold all possible emitted data.
    std::vector<uint8_t> mp3_buffer(7200);
    int flushed_bytes = lame_encode_flush(ctx.encoder.get(), mp3_buffer.data(), mp3_buffer.size());
    if (flushed_bytes < 0) {
        VIAM_SDK_LOG(error) << "LAME flush error: "
                            << mp3lame_encode_error_to_string(flushed_bytes) << " (code: " << flushed_bytes << ")";
        throw std::runtime_error("LAME encoding error during final flush");
    } else if (flushed_bytes > 0) {
        VIAM_SDK_LOG(debug) << "MP3 encoder flushed " << flushed_bytes << " bytes from internal lookahead buffer";
        output_data.insert(output_data.end(), mp3_buffer.begin(), mp3_buffer.begin() + flushed_bytes);
    }
}

void cleanup_mp3_encoder(MP3EncoderContext& ctx) {
    ctx.encoder.reset();
    ctx.sample_rate = 0;
    ctx.num_channels = 0;
    ctx.encoder_delay = 0;
}

} // namespace microphone
