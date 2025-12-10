#include "mp3_decoder.hpp"
#include <cstring>
#include <stdexcept>
#include <viam/sdk/common/utils.hpp>

namespace speaker {

// Helper to skip ID3v2 tags at the beginning of MP3 data
static size_t skip_id3v2_tag(const std::vector<uint8_t>& data) {
    // Check for ID3v2 tag (starts with "ID3")
    if (data.size() < 10 || data[0] != 'I' || data[1] != 'D' || data[2] != '3') {
        return 0;  // No ID3 tag
    }

    // ID3v2 size is stored in bytes 6-9 as a synchsafe integer (28 bits)
    // Each byte uses only 7 bits, MSB is always 0
    size_t tag_size = ((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14) | ((data[8] & 0x7F) << 7) | (data[9] & 0x7F);

    // Total size is tag_size + 10 byte header
    size_t total_size = tag_size + 10;

    VIAM_SDK_LOG(debug) << "Skipping ID3v2 tag: " << total_size << " bytes";
    return total_size;
}

MP3DecoderContext::MP3DecoderContext() : sample_rate(0), num_channels(0) {
    CleanupPtr<hip_decode_exit> hip(hip_decode_init());
    if (!hip) {
        VIAM_SDK_LOG(error) << "Failed to initialize MP3 decoder";
        throw std::runtime_error("Failed to initialize MP3 decoder");
    }

    decoder = std::move(hip);

    VIAM_SDK_LOG(debug) << "MP3 decoder initialized";
}

MP3DecoderContext::~MP3DecoderContext() {
    decoder.reset();
    sample_rate = 0;
    num_channels = 0;
}

// Helper to append PCM samples to output buffer
static void append_samples(std::vector<uint8_t>& output_data,
                           const std::vector<int16_t>& pcm_samples_left,
                           const std::vector<int16_t>& pcm_samples_right,
                           const int sample_count,
                           const int num_channels) {
    if (num_channels != 1 && num_channels != 2) {
        VIAM_SDK_LOG(error) << "invalid num channels: " << num_channels;
        throw std::invalid_argument("invalid num channels");
    }
    if (sample_count < 0) {
        VIAM_SDK_LOG(error) << "mp3 decoder: sample_count (" << std::to_string(sample_count) << ") must be non-negative";
        throw std::invalid_argument("sample_count must be non-negative");
    }
    // Bounds check to prevent buffer overflow
    if (sample_count > static_cast<int>(pcm_samples_left.size()) || sample_count > static_cast<int>(pcm_samples_right.size())) {
        VIAM_SDK_LOG(error) << "sample_count " << sample_count << " exceeds buffer size (left channel size =" << pcm_samples_left.size()
                            << ", right sample size =" << pcm_samples_right.size() << ")";
        throw std::runtime_error("sample_count exceeds pcm data buffer size");
    }
    const size_t samples_to_add = sample_count * num_channels;

    // Interleave into a int16_t buffer
    std::vector<int16_t> interleaved;
    interleaved.reserve(samples_to_add);

    if (num_channels == 1) {
        // mono: copy left channel samples
        interleaved.insert(interleaved.end(), pcm_samples_left.begin(), pcm_samples_left.begin() + sample_count);
    } else {
        // stereo: interleave L, R, L, R, ...
        for (int i = 0; i < sample_count; ++i) {
            interleaved.push_back(pcm_samples_left[i]);
            interleaved.push_back(pcm_samples_right[i]);
        }
    }

    // Append the raw bytes of interleaved samples to output_data
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(interleaved.data());
    output_data.insert(output_data.end(), bytes, bytes + interleaved.size() * sizeof(int16_t));
}

void decode_mp3_to_pcm16(MP3DecoderContext& ctx, const std::vector<uint8_t>& encoded_data, std::vector<uint8_t>& decoded_data) {
    if (!ctx.decoder) {
        VIAM_SDK_LOG(error) << "decode_mp3_to_pcm16: MP3 decoder not initialized";
        throw std::runtime_error("decode_mp3_to_pcm16: MP3 decoder not initialized");
    }

    if (encoded_data.empty()) {
        VIAM_SDK_LOG(debug) << "decode_mp3_to_pcm16: no data to decode";
        return;
    }

    // Skip ID3 tag if present
    size_t offset = skip_id3v2_tag(encoded_data);
    if (offset >= encoded_data.size()) {
        VIAM_SDK_LOG(error) << "MP3 data contains only ID3 tag, no audio frames";
        throw std::runtime_error("No MP3 audio data found, contains only ID3 tag");
    }

    VIAM_SDK_LOG(debug) << "Decoding " << (encoded_data.size() - offset) << " bytes of MP3 data (offset: " << offset << ")";

    // Buffers for decoded PCM samples - one MP3 frame is max 1152 samples
    const size_t FRAME_BUFFER_SIZE = 1152;  // Samples per channel
    std::vector<int16_t> pcm_left(FRAME_BUFFER_SIZE);
    std::vector<int16_t> pcm_right(FRAME_BUFFER_SIZE);

    mp3data_struct mp3data;
    memset(&mp3data, 0, sizeof(mp3data));

    std::vector<uint8_t> mutable_data(encoded_data.begin(), encoded_data.end());
    unsigned char* encoded_data_ptr = mutable_data.data() + offset;
    size_t mp3_data_length = mutable_data.size() - offset;
    int frames_decoded = 0;

    // Decode frame by frame using hip_decode1_headers (returns at most one frame per call)
    // Keep calling with the same data - the decoder maintains internal state
    while (true) {
        int decoded_samples =
            hip_decode1_headers(ctx.decoder.get(), encoded_data_ptr, mp3_data_length, pcm_left.data(), pcm_right.data(), &mp3data);

        if (decoded_samples < 0) {
            VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: Error decoding MP3 data";
            throw std::runtime_error("[decode_mp3_to_pcm16]: MP3 decoding error");
        }

        // Get audio properties from header
        if (ctx.sample_rate == 0 && mp3data.samplerate != 0) {
            ctx.sample_rate = mp3data.samplerate;
            ctx.num_channels = mp3data.stereo;
            VIAM_SDK_LOG(debug) << "found MP3 audio properties: " << ctx.sample_rate << "Hz, " << ctx.num_channels << " channels";
        }

        if (decoded_samples == 0) {
            // No more frames can be decoded from buffered data, break to flush
            break;
        }

        if (ctx.num_channels == 0) {
            VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: Cannot append samples: num_channels not set";
            throw std::runtime_error("[decode_mp3_to_pcm16]: MP3 properties not extracted before appending samples");
        }

        append_samples(decoded_data, pcm_left, pcm_right, decoded_samples, ctx.num_channels);
        frames_decoded++;
    }

    VIAM_SDK_LOG(debug) << "Decoded " << frames_decoded << " frames from MP3 data";

    // Flush decoder - repeatedly call with nullptr until no more samples
    int flush_count = 0;
    while (true) {
        int decoded_samples = hip_decode1_headers(ctx.decoder.get(), nullptr, 0, pcm_left.data(), pcm_right.data(), &mp3data);

        if (decoded_samples < 0) {
            VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: MP3 decoder failed to flush";
            throw std::runtime_error("[decode_mp3_to_pcm16]: MP3 decoder failed to flush");
        } else if (decoded_samples == 0) {
            VIAM_SDK_LOG(debug) << "flush returned zero samples, breaking loop";
            break;
        } else {
            // Try again to extract properties in case we didn't during initial step.
            if (ctx.sample_rate == 0 && mp3data.samplerate != 0) {
                ctx.sample_rate = mp3data.samplerate;
                ctx.num_channels = mp3data.stereo;
                VIAM_SDK_LOG(debug) << "[decode_mp3_to_pcm16]: MP3 audio properties from flush: " << ctx.sample_rate << "Hz, "
                                    << ctx.num_channels << " channels";
            }

            // Ensure we have valid properties before appending
            if (ctx.num_channels == 0) {
                VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: Cannot append samples: num_channels not set";
                throw std::runtime_error("[decode_mp3_to_pcm16]: MP3 properties not extracted before appending samples");
            }

            append_samples(decoded_data, pcm_left, pcm_right, decoded_samples, ctx.num_channels);
            flush_count++;
        }
    }

    VIAM_SDK_LOG(debug) << "[decode_mp3_to_pcm16]: Flushed " << flush_count << " additional frames";

    if (decoded_data.empty()) {
        VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: No audio data was decoded from MP3";
        throw std::runtime_error("[decode_mp3_to_pcm16]: No audio data was decoded");
    }

    // Ensure we extracted valid audio properties
    if (ctx.sample_rate == 0 || ctx.num_channels == 0) {
        VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: Failed to extract MP3 audio properties (sample_rate=" << ctx.sample_rate
                            << ", num_channels=" << ctx.num_channels << ")";
        throw std::runtime_error("[decode_mp3_to_pcm16]: Failed to extract MP3 audio properties");
    }

    VIAM_SDK_LOG(debug) << "[decode_mp3_to_pcm16]: Total decoded: " << (decoded_data.size() / sizeof(int16_t) / ctx.num_channels)
                        << " frames (" << decoded_data.size() << " bytes)";
}

}  // namespace speaker
