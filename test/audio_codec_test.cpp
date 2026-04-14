#include <gtest/gtest.h>
#include <cstdint>
#include <vector>
#include <viam/sdk/common/instance.hpp>
#include "test_utils.hpp"
#include "audio_codec.hpp"

using audio::codec::has_wav_header;
using audio::codec::wav_num_channels;
using audio::codec::wav_sample_rate;

class WavHeaderTest : public ::testing::Test {};

// Minimal valid WAV header: 44 bytes with RIFF magic, 2 channels, 44100 Hz
static std::vector<uint8_t> make_wav_header(int sample_rate, int num_channels) {
    std::vector<uint8_t> header(44, 0);
    // RIFF magic
    header[0] = 'R';
    header[1] = 'I';
    header[2] = 'F';
    header[3] = 'F';
    // num_channels at byte 22 (little-endian uint16)
    header[22] = static_cast<uint8_t>(num_channels & 0xFF);
    header[23] = static_cast<uint8_t>((num_channels >> 8) & 0xFF);
    // sample_rate at byte 24 (little-endian uint32)
    header[24] = static_cast<uint8_t>(sample_rate & 0xFF);
    header[25] = static_cast<uint8_t>((sample_rate >> 8) & 0xFF);
    header[26] = static_cast<uint8_t>((sample_rate >> 16) & 0xFF);
    header[27] = static_cast<uint8_t>((sample_rate >> 24) & 0xFF);
    return header;
}

TEST_F(WavHeaderTest, DetectsValidWavHeader) {
    const auto header = make_wav_header(44100, 2);
    EXPECT_TRUE(has_wav_header(header.data(), header.size()));
}

TEST_F(WavHeaderTest, RejectsTooSmall) {
    const std::vector<uint8_t> small = {'R', 'I', 'F', 'F'};
    EXPECT_FALSE(has_wav_header(small.data(), small.size()));
}

TEST_F(WavHeaderTest, RejectsNonRiff) {
    std::vector<uint8_t> data(44, 0);
    data[0] = 'X';
    EXPECT_FALSE(has_wav_header(data.data(), data.size()));
}

TEST_F(WavHeaderTest, RejectsEmpty) {
    EXPECT_FALSE(has_wav_header(nullptr, 0));
}

TEST_F(WavHeaderTest, ParsesNumChannelsMono) {
    const auto header = make_wav_header(48000, 1);
    EXPECT_EQ(wav_num_channels(header.data()), 1);
}

TEST_F(WavHeaderTest, ParsesNumChannelsStereo) {
    const auto header = make_wav_header(48000, 2);
    EXPECT_EQ(wav_num_channels(header.data()), 2);
}

TEST_F(WavHeaderTest, ParsesSampleRate44100) {
    const auto header = make_wav_header(44100, 2);
    EXPECT_EQ(wav_sample_rate(header.data()), 44100);
}

TEST_F(WavHeaderTest, ParsesSampleRate48000) {
    const auto header = make_wav_header(48000, 1);
    EXPECT_EQ(wav_sample_rate(header.data()), 48000);
}

TEST_F(WavHeaderTest, ParsesSampleRate16000) {
    const auto header = make_wav_header(16000, 1);
    EXPECT_EQ(wav_sample_rate(header.data()), 16000);
}

TEST_F(WavHeaderTest, ParsesCorrectlyWithAudioPayload) {
    const int sample_rate = 48000;
    const int num_channels = 2;
    auto wav = make_wav_header(sample_rate, num_channels);

    // Append 100 samples of PCM audio data after the header
    const size_t num_samples = 100;
    for (size_t i = 0; i < num_samples * sizeof(int16_t); i++) {
        wav.push_back(static_cast<uint8_t>(i & 0xFF));
    }

    EXPECT_TRUE(has_wav_header(wav.data(), wav.size()));
    EXPECT_EQ(wav_num_channels(wav.data()), num_channels);
    EXPECT_EQ(wav_sample_rate(wav.data()), sample_rate);
    EXPECT_EQ(wav.size(), audio::codec::wav_header_size + num_samples * sizeof(int16_t));
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
