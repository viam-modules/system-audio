#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include "mp3_decoder.hpp"
#include "mp3_encoder.hpp"
#include "test_utils.hpp"

using namespace speaker;
using namespace microphone;

class MP3DecoderTest : public ::testing::Test {
protected:
    std::unique_ptr<MP3DecoderContext> decoder_ctx_;
    MP3EncoderContext encoder_ctx_;

    void SetUp() override {
        decoder_ctx_ = std::make_unique<MP3DecoderContext>();
    }

    void TearDown() override {
        decoder_ctx_.reset();
        cleanup_mp3_encoder(encoder_ctx_);
    }

    // Helper to create test audio samples
    std::vector<int16_t> create_test_samples(int num_samples) {
        std::vector<int16_t> samples(num_samples);
        for (int i = 0; i < num_samples; i++) {
            // Create a simple sine-wave-like pattern for testing
            samples[i] = static_cast<int16_t>((i % 1000) * 32);
        }
        return samples;
    }

    // Helper to encode PCM samples to MP3 for testing
    std::vector<uint8_t> encode_to_mp3(const std::vector<int16_t>& samples, int sample_rate, int num_channels) {
        initialize_mp3_encoder(encoder_ctx_, sample_rate, num_channels);
        std::vector<uint8_t> encoded_data;
        encode_samples_to_mp3(encoder_ctx_, const_cast<int16_t*>(samples.data()), samples.size(), 0, encoded_data);
        flush_mp3_encoder(encoder_ctx_, encoded_data);
        return encoded_data;
    }
};

TEST_F(MP3DecoderTest, ConstructorInitializesDecoder) {
    // Decoder should be initialized by constructor
    EXPECT_NE(decoder_ctx_->decoder, nullptr);
    EXPECT_EQ(decoder_ctx_->sample_rate, 0);
    EXPECT_EQ(decoder_ctx_->num_channels, 0);
}

TEST_F(MP3DecoderTest, DecodeMonoMP3) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto test_samples = create_test_samples(1152);
    auto encoded_data = encode_to_mp3(test_samples, sample_rate, num_channels);

    std::vector<uint8_t> decoded_data;

    ASSERT_NO_THROW(decode_mp3_to_pcm16(*decoder_ctx_, encoded_data, decoded_data));

    // Verify decoder populated audio properties
    EXPECT_EQ(decoder_ctx_->sample_rate, sample_rate);
    EXPECT_EQ(decoder_ctx_->num_channels, num_channels);

    // Verify we got some decoded data
    EXPECT_FALSE(decoded_data.empty());
}

TEST_F(MP3DecoderTest, DecodeStereoMP3) {
    const int sample_rate = 44100;
    const int num_channels = 2;

    // Create test samples and encode them (stereo needs twice as many samples)
    auto test_samples = create_test_samples(1152 * 2);
    auto encoded_data = encode_to_mp3(test_samples, sample_rate, num_channels);

    std::vector<uint8_t> decoded_data;

    ASSERT_NO_THROW(decode_mp3_to_pcm16(*decoder_ctx_, encoded_data, decoded_data));

    EXPECT_EQ(decoder_ctx_->sample_rate, sample_rate);
    EXPECT_EQ(decoder_ctx_->num_channels, num_channels);

    EXPECT_FALSE(decoded_data.empty());
}

TEST_F(MP3DecoderTest, DecodeMultipleFrames) {
    const int sample_rate = 48000;
    const int num_channels = 2;

    // Create multiple frames worth of data
    auto test_samples = create_test_samples(1152 * 4 * 2);  // 4 frames, stereo
    auto encoded_data = encode_to_mp3(test_samples, sample_rate, num_channels);

    std::vector<uint8_t> decoded_data;

    ASSERT_NO_THROW(decode_mp3_to_pcm16(*decoder_ctx_, encoded_data, decoded_data));

    EXPECT_FALSE(decoded_data.empty());
    EXPECT_EQ(decoder_ctx_->sample_rate, sample_rate);
    EXPECT_EQ(decoder_ctx_->num_channels, num_channels);
}

TEST_F(MP3DecoderTest, DecodeEmptyData) {
    std::vector<uint8_t> empty_data;
    std::vector<uint8_t> decoded_data;

    // Should return without error but not decode anything
    ASSERT_NO_THROW(decode_mp3_to_pcm16(*decoder_ctx_, empty_data, decoded_data));
    EXPECT_TRUE(decoded_data.empty());
}

TEST_F(MP3DecoderTest, DecodeInvalidMP3Data) {
    // Create some random invalid data
    std::vector<uint8_t> invalid_data = {0xFF, 0xFE, 0xFD, 0xFC, 0xFB, 0xFA};
    std::vector<uint8_t> decoded_data;

    // Should throw
    EXPECT_THROW(
        decode_mp3_to_pcm16(*decoder_ctx_, invalid_data, decoded_data),
        std::runtime_error
    );
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
