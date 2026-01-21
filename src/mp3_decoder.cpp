#include "mp3_decoder.hpp"
#include <cstring>
#include <stdexcept>
#include <viam/sdk/common/utils.hpp>

namespace speaker {

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

// helper to skip id3 tag: https://id3.org/id3v2.3.0
static size_t skip_id3v2_tag(const uint8_t* data, size_t size) {
    if (size < 10) {
        return 0;
    }

    // ID3v2 header
    if (data[0] == 'I' && data[1] == 'D' && data[2] == '3') {
        // Size is stored as a "syncsafe" integer (7 bits per byte)
        size_t tag_size = ((data[6] & 0x7F) << 21) | ((data[7] & 0x7F) << 14) | ((data[8] & 0x7F) << 7) | (data[9] & 0x7F);

        // Total size = header (10 bytes) + tag payload
        return 10 + tag_size;
    }

    return 0;
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

    std::vector<uint8_t> buffer(encoded_data.begin(), encoded_data.end());

    // Skip ID3v2 tag if present
    size_t offset = skip_id3v2_tag(buffer.data(), buffer.size());
    if (offset > 0) {
        VIAM_SDK_LOG(debug) << "Skipped ID3v2 tag of size " << offset << " bytes";
    }

    // Scan for first MP3 frame sync (0xFF followed by 0xE0 mask)
    while (offset + 1 < buffer.size()) {
        if (buffer[offset] == 0xFF && (buffer[offset + 1] & 0xE0) == 0xE0) {
            break;
        }
        offset++;
    }

    if (offset >= buffer.size()) {
        VIAM_SDK_LOG(error) << "decode_mp3_to_pcm16: No MP3 frame sync found";
        throw std::runtime_error("decode_mp3_to_pcm16: MP3 decoder: no valid frame found");
    }

    unsigned char* encoded_data_ptr = buffer.data() + offset;
    const size_t mp3_data_length = buffer.size() - offset;

    VIAM_SDK_LOG(debug) << "Decoding MP3 data, buffer size after sync scan: " << mp3_data_length << " (skipped " << offset
                        << " bytes total)";

    // Buffers for decoded PCM samples - one MP3 frame is max 1152 samples
    const size_t frame_buffer_size = 1152;  // Samples per channel
    std::vector<int16_t> pcm_left(frame_buffer_size);
    std::vector<int16_t> pcm_right(frame_buffer_size);

    mp3data_struct mp3data;
    memset(&mp3data, 0, sizeof(mp3data));

    int frames_decoded = 0;

    // Feed ALL data to LAME once - it buffers internally
    // First call may return 0 while syncing, that's OK
    int decoded_samples =
        hip_decode1_headers(ctx.decoder.get(), encoded_data_ptr, mp3_data_length, pcm_left.data(), pcm_right.data(), &mp3data);

    if (decoded_samples < 0) {
        VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: Error decoding MP3 data";
        throw std::runtime_error("[decode_mp3_to_pcm16]: MP3 decoding error");
    }

    // Get audio properties
    if (mp3data.samplerate != 0) {
        ctx.sample_rate = mp3data.samplerate;
        ctx.num_channels = mp3data.stereo;
        VIAM_SDK_LOG(debug) << "found MP3 audio properties: " << ctx.sample_rate << "Hz, " << ctx.num_channels << " channels";
    }

    // Append first frame if we got samples
    if (decoded_samples > 0 && ctx.num_channels > 0) {
        append_samples(decoded_data, pcm_left, pcm_right, decoded_samples, ctx.num_channels);
        frames_decoded++;
    }

    // Now extract ALL remaining frames by calling with NULL
    // Keep going even if some calls return 0 - LAME may need multiple calls to sync
    int consecutive_zeros = 0;
    while (consecutive_zeros < 10) {  // Allow some zeros for sync, but not infinite
        decoded_samples = hip_decode1_headers(ctx.decoder.get(), nullptr, 0, pcm_left.data(), pcm_right.data(), &mp3data);

        if (decoded_samples < 0) {
            VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: Error during decode";
            break;
        }

        if (decoded_samples == 0) {
            consecutive_zeros++;
            continue;
        }

        if (consecutive_zeros > 0) {
            VIAM_SDK_LOG(debug) << "LAME synced after " << consecutive_zeros << " zero returns";
        }
        consecutive_zeros = 0;  // Reset on successful decode

        // Get audio properties if not yet set
        if (ctx.sample_rate == 0 && mp3data.samplerate != 0) {
            ctx.sample_rate = mp3data.samplerate;
            ctx.num_channels = mp3data.stereo;
            VIAM_SDK_LOG(debug) << "found MP3 audio properties: " << ctx.sample_rate << "Hz, " << ctx.num_channels << " channels";
        }

        if (ctx.num_channels > 0) {
            append_samples(decoded_data, pcm_left, pcm_right, decoded_samples, ctx.num_channels);
            frames_decoded++;
        }
    }

    // Ensure we extracted valid audio properties
    if (ctx.sample_rate == 0 || ctx.num_channels == 0) {
        VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: Failed to extract MP3 audio properties (sample_rate=" << ctx.sample_rate
                            << ", num_channels=" << ctx.num_channels << ")";
        throw std::runtime_error("[decode_mp3_to_pcm16]: Failed to extract MP3 audio properties");
    }

    if (frames_decoded == 0) {
        VIAM_SDK_LOG(error) << "[decode_mp3_to_pcm16]: No audio data decoded";
        throw std::runtime_error("[decode_mp3_to_pcm16]: decoded 0 frames");
    }

    VIAM_SDK_LOG(debug) << "[decode_mp3_to_pcm16]: Total decoded: " << (decoded_data.size() / sizeof(int16_t) / ctx.num_channels)
                        << " frames (" << decoded_data.size() << " bytes)";
}

}  // namespace speaker
