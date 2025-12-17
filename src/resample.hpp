#pragma once

#include "soxr.h"

// Note: This function expects input_data to be in PCM16
void resample_audio(int input_sample_rate,
                    int output_sample_rate,
                    int num_channels,
                    const std::vector<uint8_t>& input_data,
                    std::vector<uint8_t>& output_data) {
    VIAM_SDK_LOG(debug) << "resample_audio called: input_rate=" << input_sample_rate << " output_rate=" << output_sample_rate
                             << " channels=" << num_channels << " input_bytes=" << input_data.size();

    size_t input_samples = input_data.size() / sizeof(int16_t);
    // This formula to calculate output data length obtained from example here:
    // https://sourceforge.net/p/soxr/code/ci/master/tree/examples/1-single-block.c#l31
    size_t output_samples = (size_t)(input_samples * output_sample_rate / input_sample_rate + .5);
    VIAM_SDK_LOG(debug) << "Calculated output samples: " << output_samples;

    size_t output_done = 0;
    // Resize output to have enough space in bytes
    output_data.resize(output_samples * sizeof(int16_t));
    VIAM_SDK_LOG(debug) << "Output buffer resized to " << output_data.size() << " bytes";

    // Specify I/O format as int16 (default is float32)
    soxr_io_spec_t io_spec = soxr_io_spec(SOXR_INT16_I, SOXR_INT16_I);

    soxr_error_t err = soxr_oneshot(input_sample_rate,
                                    output_sample_rate,
                                    num_channels,
                                    reinterpret_cast<const int16_t*>(input_data.data()),
                                    input_samples,
                                    NULL,
                                    reinterpret_cast<int16_t*>(output_data.data()),
                                    output_samples,
                                    &output_done,
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

    VIAM_SDK_LOG(debug) << "Resampling successful: input_samples=" << input_samples << " output_samples_done=" << output_done
                             << " (expected ~" << output_samples << ")";

    // Resize output buffer to match actual samples written
    output_data.resize(output_done * sizeof(int16_t));
    VIAM_SDK_LOG(debug) << "Final output buffer size: " << output_data.size() << " bytes (" << output_done << " samples)";
}
