#pragma once

#include <cmath>
#include <sstream>
#include <vector>
#include "soxr.h"

// Resample PCM16 audio from one sample rate to another
// input_samples: pointer to input int16_t samples
// input_sample_count: total number of samples (frames * channels)
// output_samples: vector that will be resized and filled with resampled data
inline void resample_audio(int input_sample_rate,
                           int output_sample_rate,
                           int num_channels,
                           const int16_t* input_samples,
                           size_t input_sample_count,
                           std::vector<int16_t>& output_samples) {

    // soxr_oneshot expects "samples per channel" (frames), not total samples
    size_t input_frames = input_sample_count / num_channels;

    // This formula to calculate output data length obtained from example here:
    // https://sourceforge.net/p/soxr/code/ci/master/tree/examples/1-single-block.c#l31
    size_t output_frames = static_cast<size_t>(std::lround(input_frames * output_sample_rate / input_sample_rate));
    size_t output_sample_count = output_frames * num_channels;

    // Resize output to have enough space for samples
    output_samples.resize(output_sample_count);

    // Specify I/O format as int16 (default is float32)
    soxr_io_spec_t io_spec = soxr_io_spec(SOXR_INT16_I, SOXR_INT16_I);

    size_t output_done_frames = 0;
    soxr_error_t err = soxr_oneshot(input_sample_rate,
                                    output_sample_rate,
                                    num_channels,
                                    input_samples,
                                    input_frames,
                                    NULL,
                                    output_samples.data(),
                                    output_frames,
                                    &output_done_frames,
                                    &io_spec,
                                    NULL,
                                    NULL  // default configuration
    );
    if (err) {
        std::ostringstream buffer;
        buffer << "failed to resample: " << soxr_strerror(err);
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }

    size_t output_done_samples = output_done_frames * num_channels;

    // Resize output buffer to match actual samples written (frames * channels)
    output_samples.resize(output_done_samples);
}
