#pragma once

#include <lame/lame.h>

#include <vector>
#include <cstdint>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>

namespace microphone {
namespace vsdk = ::viam::sdk;


// Generic cleanup wrapper for functions with custom deleters
template <auto cleanup_fp>
struct Cleanup {
    using pointer_type = std::tuple_element_t<0, boost::callable_traits::args_t<decltype(cleanup_fp)>>;
    using value_type = std::remove_pointer_t<pointer_type>;

    void operator()(pointer_type p) {
        if (p != nullptr) {
            cleanup_fp(p);
        }
    }
};

template <auto cleanup_fp>
using CleanupPtr = std::unique_ptr<typename Cleanup<cleanup_fp>::value_type, Cleanup<cleanup_fp>>;

struct MP3EncoderContext {
    CleanupPtr<lame_close> encoder = nullptr;

    // Pre-allocated buffers for stereo deinterleaving (reused across calls)
    std::vector<int16_t> left_samples;
    std::vector<int16_t> right_samples;

    int sample_rate = 0;
    int num_channels = 0;

    // LAME encoder delay (samples per channel added at start)
    int encoder_delay = 0;

    // Actual frame size LAME is using (1152 for MPEG-1, 576 for MPEG-2/2.5)
    int frame_size = 0;
};


void deinterleave_samples(const std::vector<int16_t> &interleaved,
                         std::vector<int16_t> &left,
                         std::vector<int16_t> &right) noexcept;
void initialize_mp3_encoder(MP3EncoderContext& ctx, int sample_rate, int num_channels);
void flush_mp3_encoder(MP3EncoderContext& ctx, std::vector<uint8_t>& output_data);
void cleanup_mp3_encoder(MP3EncoderContext& ctx);
void encode_samples_to_mp3(MP3EncoderContext& ctx,
                               const int16_t* samples,
                               int sample_count,
                               uint64_t chunk_start_position,
                               std::vector<uint8_t>& output_data);

} // namespace microphone
