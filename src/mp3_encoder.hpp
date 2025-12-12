#pragma once

#include <lame/lame.h>

#include <cstdint>
#include <vector>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include "audio_utils.hpp"

namespace microphone {
namespace vsdk = ::viam::sdk;

// 192 kbps bit rate - how many bits of audio used to represent one second of audio
// higher bitrate = better quality,larger file size
constexpr int MP3_BIT_RATE = 192;
// (0=best, 9=worst)
// higher quality = slower
constexpr int MP3_QUALITY = 2;

using audio::utils::CleanupPtr;

struct MP3EncoderContext {
    CleanupPtr<lame_close> encoder = nullptr;

    int sample_rate = 0;
    int num_channels = 0;

    // LAME encoder delay (samples per channel added at start)
    int encoder_delay = 0;

    int frame_size = 0;
};
void initialize_mp3_encoder(MP3EncoderContext& ctx, int sample_rate, int num_channels);
void flush_mp3_encoder(MP3EncoderContext& ctx, std::vector<uint8_t>& output_data);
void cleanup_mp3_encoder(MP3EncoderContext& ctx);
void encode_samples_to_mp3(
    MP3EncoderContext& ctx, int16_t* samples, int sample_count, uint64_t chunk_start_position, std::vector<uint8_t>& output_data);

}  // namespace microphone
