#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include "mp3_encoder.hpp"

namespace audio {
namespace codec {

// Audio codec types supported by the microphone
enum class AudioCodec { PCM_16, PCM_32, PCM_32_FLOAT, MP3 };

// Convert string to lowercase
std::string toLower(std::string s);

// Parse codec string to AudioCodec enum
// Throws std::invalid_argument if codec is unsupported
AudioCodec parse_codec(const std::string& codec_str);

// Convert PCM16 samples to PCM32 format
// Each int16_t sample is converted to int32_t and written as little-endian bytes
void convert_pcm16_to_pcm32(const int16_t* samples, int sample_count, std::vector<uint8_t>& output);

// Convert PCM16 samples to 32-bit float format
// Each int16_t sample is normalized to [-1.0, 1.0] range
void convert_pcm16_to_float32(const int16_t* samples, int sample_count, std::vector<uint8_t>& output);

// Copy PCM16 samples to output buffer
// Converts from int16_t array to uint8_t vector (2 bytes per sample)
void copy_pcm16(const int16_t* samples, int sample_count, std::vector<uint8_t>& output);

void convert_pcm32_to_pcm16(const uint8_t* input_data, int byte_count, std::vector<uint8_t>& output);
void convert_float32_to_pcm16(const uint8_t* input_data, int byte_count, std::vector<uint8_t>& output);

// Encode audio chunk based on codec type
// Dispatches to appropriate encoding function based on codec
void encode_audio_chunk(AudioCodec codec,
                        int16_t* samples,
                        int sample_count,
                        uint64_t chunk_start_position,
                        microphone::MP3EncoderContext& mp3_ctx,
                        std::vector<uint8_t>& output_data);

}  // namespace codec
}  // namespace audio
