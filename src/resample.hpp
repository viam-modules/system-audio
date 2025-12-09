#pragma once

#include "soxr-lsr.h"
#include "soxr.h"

void resample_audio(int input_sample_rate,
                    int output_sample_rate,
                    int num_channels,
                    const std::vector<uint8_t>& input_data,
                    std::vector<uint8_t>& output_data) {
    size_t input_samples = input_data.size() / sizeof(int16_t);
    // This formula to calculate output data length obtained from example here:
    // https://sourceforge.net/p/soxr/code/ci/master/tree/examples/1-single-block.c#l31
    size_t output_samples = (size_t)(input_samples * output_sample_rate / input_sample_rate + .5);
    size_t output_done = 0;
    // Resize output to have enough space in bytes
    output_data.resize(output_samples * sizeof(int16_t));
    soxr_error_t err = soxr_oneshot(input_sample_rate,   // input rate
                                    output_sample_rate,  // output rate
                                    num_channels,
                                    reinterpret_cast<int16_t*>(output_data.data()),
                                    input_samples,
                                    NULL,
                                    output_data.data(),
                                    output_samples,
                                    &output_done,
                                    NULL,
                                    NULL,
                                    NULL  // default configuration
    );
    if (err) {
        std::ostringstream buffer;
        buffer << "failed to resample: " << soxr_strerror(err);
        VIAM_SDK_LOG(error) << buffer.str();
        throw std::runtime_error(buffer.str());
    }

    VIAM_SDK_LOG(debug) << "Resampled " << input_samples << " to " << output_done << " samples";
}
