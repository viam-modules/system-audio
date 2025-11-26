#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include "mp3_encoder.hpp"
#include "audio_stream.hpp"

using namespace microphone;

class MP3EncoderTestEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        instance_ = std::make_unique<viam::sdk::Instance>();
    }
    void TearDown() override {
        instance_.reset();
    }
private:
    std::unique_ptr<viam::sdk::Instance> instance_;
};
class MP3EncoderTest : public ::testing::Test {
protected:
    MP3EncoderContext ctx_;

    void TearDown() override {
        cleanup_mp3_encoder(ctx_);
    }

    // Helper to create test audio samples
    std::vector<int16_t> create_test_samples(int num_samples) {
        std::vector<int16_t> samples(num_samples);
        for (int i = 0; i < num_samples; i++) {
            samples[i] = i;
        }
        return samples;
    }
};

TEST_F(MP3EncoderTest, InitializeSucceeds) {
    ASSERT_NO_THROW(initialize_mp3_encoder(ctx_, 48000, 2));

    EXPECT_NE(ctx_.encoder, nullptr);
    EXPECT_EQ(ctx_.sample_rate, 48000);
    EXPECT_EQ(ctx_.num_channels, 2);
    EXPECT_NE(ctx_.encoder_delay, 0);
}

// Test encoding exactly one MP3 frame
TEST_F(MP3EncoderTest, EncodeOneCompleteMp3Frame) {
    const int sample_rate = 48000;
    initialize_mp3_encoder(ctx_, sample_rate, 2);

    auto samples = create_test_samples(1152);
    std::vector<uint8_t> output;

    encode_samples_to_mp3(ctx_, samples.data(), samples.size(), 0, output);

    EXPECT_FALSE(output.empty());
}

TEST_F(MP3EncoderTest, EncodeMultipleMp3Frames) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    auto samples = create_test_samples(4032 * 2);
    std::vector<uint8_t> output;

    encode_samples_to_mp3(ctx_, samples.data(), samples.size(), 0, output);
    EXPECT_FALSE(output.empty());
}


TEST_F(MP3EncoderTest, FlushEncoder) {
    const int sample_rate = 48000;
    initialize_mp3_encoder(ctx_, sample_rate, 2);

    // aligned chunks
    auto samples = create_test_samples(1152*2);
    std::vector<uint8_t> output;
    encode_samples_to_mp3(ctx_, samples.data(), samples.size(), 0, output);


    std::vector<uint8_t> flush_output;
    flush_mp3_encoder(ctx_, flush_output);
    EXPECT_FALSE(flush_output.empty());
}

TEST_F(MP3EncoderTest, FlushEncoderUnalginedChunks) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    // aligned chunks
    auto samples = create_test_samples(5000);
    std::vector<uint8_t> output;
    encode_samples_to_mp3(ctx_, samples.data(), samples.size(), 0, output);


    std::vector<uint8_t> flush_output;
    flush_mp3_encoder(ctx_, flush_output);
    EXPECT_FALSE(flush_output.empty());
}

TEST_F(MP3EncoderTest, CleanupEncoder) {
    initialize_mp3_encoder(ctx_, 48000, 2);

    EXPECT_NE(ctx_.encoder, nullptr);

    cleanup_mp3_encoder(ctx_);

    EXPECT_EQ(ctx_.encoder, nullptr);
    EXPECT_EQ(ctx_.sample_rate, 0);
    EXPECT_EQ(ctx_.num_channels, 0);
}

TEST_F(MP3EncoderTest, EncodeWithoutInitialization) {
    auto samples = create_test_samples(1152);
    std::vector<uint8_t> output;

    // Should throw because encoder is not initialized
    EXPECT_THROW(
        encode_samples_to_mp3(ctx_, samples.data(), samples.size(), 0, output),
        std::runtime_error
    );
}

TEST_F(MP3EncoderTest, EncodeDoesNothingIfEmptySamples) {
    initialize_mp3_encoder(ctx_, 48000, 2);
    std::vector<uint8_t> output;
    auto samples = create_test_samples(1);

    // Should throw because length is zero
    ASSERT_NO_THROW(
        encode_samples_to_mp3(ctx_, samples.data(), 0, 0, output));
}

TEST_F(MP3EncoderTest, EncodeNullSamples) {
    initialize_mp3_encoder(ctx_, 48000, 2);
    std::vector<uint8_t> output;

    // Should throw becausse samples array is null
    EXPECT_THROW(
        encode_samples_to_mp3(ctx_, nullptr, 0, 0, output),
        std::invalid_argument
    );
}


TEST_F(MP3EncoderTest, InitializeDifferentConfigs) {
    ASSERT_NO_THROW(initialize_mp3_encoder(ctx_, 44100, 2));
    EXPECT_EQ(ctx_.sample_rate, 44100);
    cleanup_mp3_encoder(ctx_);

    ASSERT_NO_THROW(initialize_mp3_encoder(ctx_, 16000, 2));
    EXPECT_EQ(ctx_.sample_rate, 16000);
    cleanup_mp3_encoder(ctx_);

    ASSERT_NO_THROW(initialize_mp3_encoder(ctx_, 8000, 1));
    EXPECT_EQ(ctx_.sample_rate, 8000);
    EXPECT_EQ(ctx_.num_channels, 1);
}


TEST_F(MP3EncoderTest, FlushUninitializedEncoder) {
    std::vector<uint8_t> output;

    EXPECT_THROW(
        flush_mp3_encoder(ctx_, output),
        std::invalid_argument
    );
}


int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new MP3EncoderTestEnvironment);
  return RUN_ALL_TESTS();
}
