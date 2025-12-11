#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/config/resource.hpp>
#include <cmath>
#include "speaker.hpp"
#include "test_utils.hpp"

using namespace viam::sdk;
using namespace audio;

class SpeakerTest: public test_utils::AudioTestBase {
protected:
    void SetUp() override {
        AudioTestBase::SetUp();

        test_name_ = "test_audioout";
        test_deps_ = Dependencies{};

        auto attributes = ProtoStruct{};
        test_config_ = std::make_unique<ResourceConfig>(
            "rdk:component:audioout", "", test_name_, attributes, "",
            Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info
        );

        SetupDefaultPortAudioBehavior();
    }

    std::string test_name_;
    Dependencies test_deps_;
    std::unique_ptr<ResourceConfig> test_config_;
};


TEST_F(SpeakerTest, ValidateWithValidConfig) {
  auto attributes = ProtoStruct{};

  ResourceConfig valid_config(
      "rdk:component:audioout", "", test_name_, attributes, "",
      Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info);

  EXPECT_NO_THROW({
    auto result = speaker::Speaker::validate(valid_config);
    EXPECT_TRUE(result.empty());
  });
}

TEST_F(SpeakerTest, ValidateWithValidOptionalAttributes) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = test_name_;
  attributes["latency"] = 1.0;

  ResourceConfig valid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info);

  EXPECT_NO_THROW({
    auto result = speaker::Speaker::validate(valid_config);
    EXPECT_TRUE(result.empty());
  });
}

TEST_F(SpeakerTest, ValidateWithDeviceNameNotString) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = 2;
  attributes["latency"] = 1.0;

  ResourceConfig invalid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info);

  EXPECT_THROW({
    speaker::Speaker::validate(invalid_config); },
    std::invalid_argument);
}


TEST_F(SpeakerTest, ValidateWithLatencyNotDouble) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = test_name_;
  attributes["latency"] = "2";

  ResourceConfig invalid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info);

  EXPECT_THROW({
    speaker::Speaker::validate(invalid_config); },
    std::invalid_argument);
}


TEST_F(SpeakerTest, GetPropertiesReturnsCorrectValues) {
    int sample_rate = 48000;
    int num_channels = 2;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
        Model("viam", "audio", "speaker"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};

    speaker::Speaker speaker(deps, config, mock_pa_.get());

    ProtoStruct extra{};
    auto props = speaker.get_properties(extra);

    EXPECT_EQ(props.sample_rate_hz, sample_rate);
    EXPECT_EQ(props.num_channels, num_channels);
    ASSERT_EQ(props.supported_codecs.size(), 1);
    EXPECT_EQ(props.supported_codecs[0], viam::sdk::audio_codecs::PCM_16);

}

TEST_F(SpeakerTest, PlayWithValidPCM16Data) {
    int sample_rate = 48000;
    int num_channels = 2;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
        Model("viam", "audio", "speaker"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Create 100ms of stereo audio (48000 samples/sec * 0.1 sec * 2 channels = 9600 samples)
    int num_samples = 9600;
    std::vector<uint8_t> audio_data(num_samples * sizeof(int16_t));
    int16_t* samples = reinterpret_cast<int16_t*>(audio_data.data());

    for (int i = 0; i < num_samples; i++) {
        samples[i] = i;
    }

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, num_channels};
    ProtoStruct extra{};

    speaker.audio_context_->playback_position.store(num_samples);

    EXPECT_NO_THROW({
        speaker.play(audio_data, info, extra);
    });
}


TEST_F(SpeakerTest, PlayWithUnsupportedCodec) {
    int sample_rate = 48000;
    int num_channels = 2;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
        Model("viam", "audio", "speaker"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    std::vector<uint8_t> audio_data(4800);

    viam::sdk::audio_info info{"opus", sample_rate, num_channels};
    ProtoStruct extra{};

    EXPECT_THROW({
        speaker.play(audio_data, info, extra);
    }, std::invalid_argument);
}

TEST_F(SpeakerTest, PlayWithOddByteCount) {
    int sample_rate = 48000;
    int num_channels = 2;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
        Model("viam", "audio", "speaker"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Create audio data with odd byte count (invalid for PCM_16)
    std::vector<uint8_t> audio_data(4801);

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, num_channels};
    ProtoStruct extra{};

    EXPECT_THROW({
        speaker.play(audio_data, info, extra);
    }, std::invalid_argument);
}

TEST_F(SpeakerTest, PlayEmptyData) {
    int sample_rate = 48000;
    int num_channels = 2;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
        Model("viam", "audio", "speaker"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    std::vector<uint8_t> audio_data;

    ProtoStruct extra{};

    // Playing empty data should work (just does nothing)
    EXPECT_NO_THROW({
        speaker.play(audio_data, boost::none, extra);
    });
}

TEST_F(SpeakerTest, CallbackWithNoData) {
    // Create an OutputStreamContext with no data written yet
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 48000, 2};
    audio::OutputStreamContext ctx(info, 30);

    // Prepare output buffer
    const int frames_per_buffer = 256;
    const int total_samples = frames_per_buffer * info.num_channels;
    std::vector<int16_t> output_buffer(total_samples);

    // Call the callback with empty buffer
    int result = speaker::speakerCallback(
        nullptr,                     // inputBuffer (not used)
        output_buffer.data(),        // outputBuffer
        frames_per_buffer,           // framesPerBuffer
        nullptr,                     // timeInfo (not used)
        0,                          // statusFlags
        &ctx                        // userData
    );

    // Should return paContinue
    EXPECT_EQ(result, paContinue);

    // Output should be all zeros (silence)
    for (int i = 0; i < total_samples; i++) {
        EXPECT_EQ(output_buffer[i], 0);
    }
}

TEST_F(SpeakerTest, CallbackWithNullUserData) {
    const int frames_per_buffer = 256;
    std::vector<int16_t> output_buffer(frames_per_buffer * 2);

    // Call with null userData should return paAbort
    int result = speaker::speakerCallback(
        nullptr,
        output_buffer.data(),
        frames_per_buffer,
        nullptr,
        0,
        nullptr  // null userData
    );

    EXPECT_EQ(result, paAbort);
}

TEST_F(SpeakerTest, CallbackWithNullOutputBuffer) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 48000, 2};
    audio::OutputStreamContext ctx(info, 30);

    const int frames_per_buffer = 256;

    // Call with null outputBuffer should return paAbort
    int result = speaker::speakerCallback(
        nullptr,
        nullptr,  // null outputBuffer
        frames_per_buffer,
        nullptr,
        0,
        &ctx
    );

    EXPECT_EQ(result, paAbort);
}

TEST_F(SpeakerTest, CallbackReadsDataFromContext) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 48000, 2};
    audio::OutputStreamContext ctx(info, 30);

    const int num_test_samples = 1000;
    for (int i = 0; i < num_test_samples; i++) {
        ctx.write_sample(static_cast<int16_t>(i));
    }

    const int frames_per_buffer = 256;
    const int total_samples = frames_per_buffer * info.num_channels;
    std::vector<int16_t> output_buffer(total_samples);
    int result = speaker::speakerCallback(
        nullptr,
        output_buffer.data(),
        frames_per_buffer,
        nullptr,
        0,
        &ctx
    );

    EXPECT_EQ(result, paContinue);

    for (int i = 0; i < total_samples && i < num_test_samples; i++) {
        EXPECT_EQ(output_buffer[i], static_cast<int16_t>(i));
    }

    EXPECT_EQ(ctx.playback_position.load(), total_samples);
}

TEST_F(SpeakerTest, CallbackFillsWithSilenceWhenInsufficientData) {
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 48000, 2};
    audio::OutputStreamContext ctx(info, 30);

    const int num_test_samples = 100;
    for (int i = 0; i < num_test_samples; i++) {
        ctx.write_sample(static_cast<int16_t>(1000 + i));
    }

    // Prepare output buffer for callback (requesting more samples than available)
    const int frames_per_buffer = 256;
    const int total_samples = frames_per_buffer * info.num_channels;
    std::vector<int16_t> output_buffer(total_samples);

    int result = speaker::speakerCallback(
        nullptr,
        output_buffer.data(),
        frames_per_buffer,
        nullptr,
        0,
        &ctx
    );

    EXPECT_EQ(result, paContinue);

    // First num_test_samples should contain our data
    for (int i = 0; i < num_test_samples; i++) {
        EXPECT_EQ(output_buffer[i], static_cast<int16_t>(1000 + i));
    }

    // Remaining samples should be silence (0)
    for (int i = num_test_samples; i < total_samples; i++) {
        EXPECT_EQ(output_buffer[i], 0);
    }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
  return RUN_ALL_TESTS();
}



