#include "audio_codec.hpp"
#include <algorithm>
#include <cctype>
#include <cstring>
#include <sstream>
#include <viam/sdk/components/audio_in.hpp>
#include "audio_stream.hpp"

namespace audio {
namespace codec {

namespace vsdk = ::viam::sdk;

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

AudioCodec parse_codec(const std::string& codec_str) {
    std::string codec = toLower(codec_str);
    if (codec == vsdk::audio_codecs::PCM_32) {
        return AudioCodec::PCM_32;
    } else if (codec == vsdk::audio_codecs::PCM_32_FLOAT) {
        return AudioCodec::PCM_32_FLOAT;
    } else if (codec == vsdk::audio_codecs::MP3) {
        VIAM_SDK_LOG(info) << "is mp3";
        return AudioCodec::MP3;
    } else if (codec == vsdk::audio_codecs::PCM_16) {
        return AudioCodec::PCM_16;
    } else {
        std::ostringstream buffer;
        buffer << "Unsupported codec: " << codec << ". Supported codecs: pcm16, pcm32, pcm32_float, mp3";
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::invalid_argument(buffer.str());
    }
}

void convert_pcm16_to_pcm32(const int16_t* samples, int sample_count, std::vector<uint8_t>& output) {
    if (samples == nullptr || sample_count <= 0) {
        output.clear();
        return;
    }

    // Convert int16 to int32 (left shift by 16 to preserve volume)
    output.resize(sample_count * sizeof(int32_t));
    int32_t* out = reinterpret_cast<int32_t*>(output.data());
    for (int i = 0; i < sample_count; i++) {
        out[i] = static_cast<int32_t>(samples[i]) << 16;
    }
}

void convert_pcm16_to_float32(const int16_t* samples, int sample_count, std::vector<uint8_t>& output) {
    if (samples == nullptr || sample_count <= 0) {
        output.clear();
        return;
    }

    // Convert int16 to float32 (normalize to range -1.0 to 1.0)
    output.resize(sample_count * sizeof(float));
    float* out = reinterpret_cast<float*>(output.data());
    for (int i = 0; i < sample_count; i++) {
        out[i] = static_cast<float>(samples[i]) * audio::INT16_TO_FLOAT_SCALE;
    }
}

void copy_pcm16(const int16_t* samples, int sample_count, std::vector<uint8_t>& output) {
    if (samples == nullptr || sample_count <= 0) {
        output.clear();
        return;
    }

    output.resize(sample_count * sizeof(int16_t));
    std::memcpy(output.data(), samples, sample_count * sizeof(int16_t));
}

// Convert PCM32 to PCM16
void convert_pcm32_to_pcm16(const uint8_t* input_data, int byte_count, std::vector<uint8_t>& output) {
    if (byte_count % 4 != 0) {
        throw std::invalid_argument("PCM32 data size must be divisible by 4");
    }

    int sample_count = byte_count / sizeof(int32_t);
    output.resize(sample_count * sizeof(int16_t));

    const int32_t* input = reinterpret_cast<const int32_t*>(input_data);
    int16_t* output_samples = reinterpret_cast<int16_t*>(output.data());

    for (int i = 0; i < sample_count; i++) {
        // Right shift by 16 to convert int32 back to int16
        output_samples[i] = static_cast<int16_t>(input[i] >> 16);
    }
}

// Convert PCM32_FLOAT to PCM16
void convert_float32_to_pcm16(const uint8_t* input_data, int byte_count, std::vector<uint8_t>& output) {
    if (byte_count % 4 != 0) {
        throw std::invalid_argument("Float32 data size must be divisible by 4");
    }

    int sample_count = byte_count / sizeof(float);
    output.resize(sample_count * sizeof(int16_t));

    const float* input = reinterpret_cast<const float*>(input_data);
    int16_t* output_samples = reinterpret_cast<int16_t*>(output.data());

    for (int i = 0; i < sample_count; i++) {
        // Clamp float to [-1.0, 1.0] and scale to int16 range
        float clamped = std::max(-1.0f, std::min(1.0f, input[i]));
        output_samples[i] = static_cast<int16_t>(clamped * 32767.0f);
    }
}

void encode_audio_chunk(AudioCodec codec,
                        int16_t* samples,
                        int sample_count,
                        uint64_t chunk_start_position,
                        microphone::MP3EncoderContext& mp3_ctx,
                        std::vector<uint8_t>& output_data) {
    switch (codec) {
        case AudioCodec::PCM_16:
            copy_pcm16(samples, sample_count, output_data);
            break;
        case AudioCodec::PCM_32:
            convert_pcm16_to_pcm32(samples, sample_count, output_data);
            break;
        case AudioCodec::PCM_32_FLOAT:
            convert_pcm16_to_float32(samples, sample_count, output_data);
            break;
        case AudioCodec::MP3:
            microphone::encode_samples_to_mp3(mp3_ctx, samples, sample_count, chunk_start_position, output_data);
            break;
        default:
            throw std::invalid_argument("Unsupported codec for encoding");
    }
}

}  // namespace codec
}  // namespace audio
