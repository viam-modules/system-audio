#include <gtest/gtest.h>
#include <vector>
#include <cstdint>
#include <viam/sdk/common/instance.hpp>
#include "test_utils.hpp"
#include "resample.hpp"

class ConvertChannelsTest : public ::testing::Test {};

TEST_F(ConvertChannelsTest, MonoToStereo) {
    const std::vector<int16_t> mono = {100, 200, 300, 400};
    std::vector<int16_t> output;

    convert_channels(mono.data(), mono.size(), 1, 2, output);

    const std::vector<int16_t> expected = {100, 100, 200, 200, 300, 300, 400, 400};
    EXPECT_EQ(output, expected);
}

TEST_F(ConvertChannelsTest, StereoToMono) {
    // Stereo interleaved: L, R, L, R
    const std::vector<int16_t> stereo = {100, 200, 300, 400};
    std::vector<int16_t> output;

    convert_channels(stereo.data(), stereo.size(), 2, 1, output);

    // Average: (100+200)/2=150, (300+400)/2=350
    const std::vector<int16_t> expected = {150, 350};
    EXPECT_EQ(output, expected);
}

TEST_F(ConvertChannelsTest, StereoToMonoNegativeValues) {
    const std::vector<int16_t> stereo = {-1000, 1000, -500, -500};
    std::vector<int16_t> output;

    convert_channels(stereo.data(), stereo.size(), 2, 1, output);

    // Average: (-1000+1000)/2=0, (-500+-500)/2=-500
    const std::vector<int16_t> expected = {0, -500};
    EXPECT_EQ(output, expected);
}

TEST_F(ConvertChannelsTest, StereoToMonoExtremeValues) {
    // Verify int32 intermediate prevents overflow
    const std::vector<int16_t> stereo = {INT16_MAX, INT16_MAX};
    std::vector<int16_t> output;

    convert_channels(stereo.data(), stereo.size(), 2, 1, output);

    const std::vector<int16_t> expected = {INT16_MAX};
    EXPECT_EQ(output, expected);
}

TEST_F(ConvertChannelsTest, MonoToStereoSingleSample) {
    const std::vector<int16_t> mono = {42};
    std::vector<int16_t> output;

    convert_channels(mono.data(), mono.size(), 1, 2, output);

    const std::vector<int16_t> expected = {42, 42};
    EXPECT_EQ(output, expected);
}

TEST_F(ConvertChannelsTest, UnsupportedConversionThrows) {
    const std::vector<int16_t> input = {100, 200, 300};
    std::vector<int16_t> output;

    EXPECT_THROW(convert_channels(input.data(), input.size(), 1, 3, output), std::invalid_argument);
    EXPECT_THROW(convert_channels(input.data(), input.size(), 3, 1, output), std::invalid_argument);
    EXPECT_THROW(convert_channels(input.data(), input.size(), 2, 2, output), std::invalid_argument);
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
