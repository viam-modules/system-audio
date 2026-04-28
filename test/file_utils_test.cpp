#include <gtest/gtest.h>

#include "file_utils.hpp"

using audio::utils::AlsaHw;
using audio::utils::parse_alsa_hw;

TEST(ParseAlsaHw, FullPortAudioDeviceName) {
    const auto hw = parse_alsa_hw("USB PnP Sound Device: Audio (hw:1,0)");
    ASSERT_TRUE(hw.has_value());
    EXPECT_EQ(hw->card_num, 1);
    EXPECT_EQ(hw->device_num, 0);
}

TEST(ParseAlsaHw, BareHwSuffix) {
    const auto hw = parse_alsa_hw("(hw:0,3)");
    ASSERT_TRUE(hw.has_value());
    EXPECT_EQ(hw->card_num, 0);
    EXPECT_EQ(hw->device_num, 3);
}

TEST(ParseAlsaHw, MultiDigitDeviceNumber) {
    const auto hw = parse_alsa_hw("NVIDIA Jetson Orin Nano APE: - (hw:1,19)");
    ASSERT_TRUE(hw.has_value());
    EXPECT_EQ(hw->card_num, 1);
    EXPECT_EQ(hw->device_num, 19);
}

TEST(ParseAlsaHw, MultiDigitCardNumber) {
    const auto hw = parse_alsa_hw("Some Card (hw:12,4)");
    ASSERT_TRUE(hw.has_value());
    EXPECT_EQ(hw->card_num, 12);
    EXPECT_EQ(hw->device_num, 4);
}

TEST(ParseAlsaHw, MissingParensReturnsNullopt) {
    EXPECT_FALSE(parse_alsa_hw("hw:1,0").has_value());
}

TEST(ParseAlsaHw, MissingDeviceNumberReturnsNullopt) {
    EXPECT_FALSE(parse_alsa_hw("(hw:5)").has_value());
}

TEST(ParseAlsaHw, EmptyStringReturnsNullopt) {
    EXPECT_FALSE(parse_alsa_hw("").has_value());
}

TEST(ParseAlsaHw, AlsaVirtualEndpointsReturnNullopt) {
    EXPECT_FALSE(parse_alsa_hw("default").has_value());
    EXPECT_FALSE(parse_alsa_hw("pulse").has_value());
    EXPECT_FALSE(parse_alsa_hw("hdmi").has_value());
}

TEST(ParseAlsaHw, CoreAudioStyleNameReturnsNullopt) {
    EXPECT_FALSE(parse_alsa_hw("Built-in Microphone").has_value());
    EXPECT_FALSE(parse_alsa_hw("MacBook Pro Speakers").has_value());
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
