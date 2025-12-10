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
        decoder_ctx_ = std::make_unique<MP3DecoderContext>();  // Constructor initializes
    }

    void TearDown() override {
        decoder_ctx_.reset();  // Destructor cleans up
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

    // Create test samples and encode them
    auto test_samples = create_test_samples(1152);
    auto encoded_data = encode_to_mp3(test_samples, sample_rate, num_channels);

    // Decode (decoder already initialized by constructor)
    std::vector<uint8_t> decoded_data;

    ASSERT_NO_THROW(decode_mp3_to_pcm16(*decoder_ctx_, encoded_data, decoded_data));

    // Verify decoder populated audio properties
    EXPECT_EQ(decoder_ctx_->sample_rate, sample_rate);
    EXPECT_EQ(decoder_ctx_->num_channels, num_channels);

    // Verify we got some decoded data
    EXPECT_FALSE(decoded_data.empty());

    int decoded_samples = decoded_data.size() / sizeof(int16_t);
    EXPECT_GT(decoded_samples, 0);
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

TEST_F(MP3DecoderTest, DecodeConsecutiveChunks) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    // Decode first chunk
    auto samples1 = create_test_samples(1152);
    auto encoded1 = encode_to_mp3(samples1, sample_rate, num_channels);
    std::vector<uint8_t> decoded1;
    decode_mp3_to_pcm16(*decoder_ctx_, encoded1, decoded1);

    EXPECT_FALSE(decoded1.empty());
    EXPECT_EQ(decoder_ctx_->sample_rate, sample_rate);

    // Create new decoder for second chunk
    decoder_ctx_ = std::make_unique<MP3DecoderContext>();

    // Decode second chunk
    auto samples2 = create_test_samples(2304);
    auto encoded2 = encode_to_mp3(samples2, sample_rate, num_channels);
    std::vector<uint8_t> decoded2;
    decode_mp3_to_pcm16(*decoder_ctx_, encoded2, decoded2);

    EXPECT_FALSE(decoded2.empty());
}

TEST_F(MP3DecoderTest, DecodeDifferentSampleRates) {
    // Test 44.1kHz
    {
        decoder_ctx_ = std::make_unique<MP3DecoderContext>();
        auto samples = create_test_samples(1152);
        auto encoded = encode_to_mp3(samples, 44100, 1);
        std::vector<uint8_t> decoded;
        decode_mp3_to_pcm16(*decoder_ctx_, encoded, decoded);
        EXPECT_EQ(decoder_ctx_->sample_rate, 44100);
    }

    // Test 16kHz
    {
        decoder_ctx_ = std::make_unique<MP3DecoderContext>();
        auto samples = create_test_samples(1152);
        auto encoded = encode_to_mp3(samples, 16000, 1);
        std::vector<uint8_t> decoded;
        decode_mp3_to_pcm16(*decoder_ctx_, encoded, decoded);
        EXPECT_EQ(decoder_ctx_->sample_rate, 16000);
    }

    // Test 8kHz
    {
        decoder_ctx_ = std::make_unique<MP3DecoderContext>();
        auto samples = create_test_samples(1152);
        auto encoded = encode_to_mp3(samples, 8000, 1);
        std::vector<uint8_t> decoded;
        decode_mp3_to_pcm16(*decoder_ctx_, encoded, decoded);
        EXPECT_EQ(decoder_ctx_->sample_rate, 8000);
    }
}

TEST_F(MP3DecoderTest, DecodeOutputIsInterleavedForStereo) {
    const int sample_rate = 48000;
    const int num_channels = 2;

    auto test_samples = create_test_samples(1152 * 2);
    auto encoded_data = encode_to_mp3(test_samples, sample_rate, num_channels);

    std::vector<uint8_t> decoded_data;
    decode_mp3_to_pcm16(*decoder_ctx_, encoded_data, decoded_data);

    EXPECT_FALSE(decoded_data.empty());

    // For stereo, decoded data should be interleaved (L, R, L, R, ...)
    // So the number of samples should be divisible by num_channels
    int total_samples = decoded_data.size() / sizeof(int16_t);
    EXPECT_EQ(total_samples % num_channels, 0);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
