#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/config/resource.hpp>

#include <chrono>
#include <cmath>
#include <thread>
#include "speaker.hpp"
#include "test_utils.hpp"
#include "audio_codec.hpp"
#include "mp3_encoder.hpp"

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
            Model("viam", "system-audio", "speaker"), LinkConfig{}, log_level::info
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
  speaker::Speaker::model, LinkConfig{}, log_level::info);

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
   speaker::Speaker::model, LinkConfig{}, log_level::info);

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
  speaker::Speaker::model, LinkConfig{}, log_level::info);

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
    speaker::Speaker::model, LinkConfig{}, log_level::info);

  EXPECT_THROW({
    speaker::Speaker::validate(invalid_config); },
    std::invalid_argument);
}

TEST_F(SpeakerTest, ValidateWithValidVolume) {
  auto attributes = ProtoStruct{};
  attributes["volume"] = 50;

  ResourceConfig valid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      speaker::Speaker::model, LinkConfig{}, log_level::info);

  EXPECT_NO_THROW({
    auto result = speaker::Speaker::validate(valid_config);
    EXPECT_TRUE(result.empty());
  });
}

TEST_F(SpeakerTest, ValidateWithVolumeBoundaries) {
  auto attrs_zero = ProtoStruct{};
  attrs_zero["volume"] = 0;

  ResourceConfig config_zero(
      "rdk:component:speaker", "", test_name_, attrs_zero, "",
      speaker::Speaker::model, LinkConfig{}, log_level::info);

  EXPECT_NO_THROW(speaker::Speaker::validate(config_zero));

  auto attrs_max = ProtoStruct{};
  attrs_max["volume"] = 100.0;

  ResourceConfig config_max(
      "rdk:component:speaker", "", test_name_, attrs_max, "",
      speaker::Speaker::model, LinkConfig{}, log_level::info);

  EXPECT_NO_THROW(speaker::Speaker::validate(config_max));
}

TEST_F(SpeakerTest, ValidateWithVolumeNotNumber) {
  auto attributes = ProtoStruct{};
  attributes["volume"] = "loud";

  ResourceConfig invalid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      speaker::Speaker::model, LinkConfig{}, log_level::info);

  EXPECT_THROW(speaker::Speaker::validate(invalid_config), std::invalid_argument);
}

TEST_F(SpeakerTest, ValidateWithVolumeTooHigh) {
  auto attributes = ProtoStruct{};
  attributes["volume"] = 101;

  ResourceConfig invalid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      speaker::Speaker::model, LinkConfig{}, log_level::info);

  EXPECT_THROW(speaker::Speaker::validate(invalid_config), std::invalid_argument);
}

TEST_F(SpeakerTest, ValidateWithVolumeTooLow) {
  auto attributes = ProtoStruct{};
  attributes["volume"] = -1;

  ResourceConfig invalid_config(
      "rdk:component:speaker", "", test_name_, attributes, "",
      speaker::Speaker::model, LinkConfig{}, log_level::info);

  EXPECT_THROW(speaker::Speaker::validate(invalid_config), std::invalid_argument);
}

TEST_F(SpeakerTest, DoCommandSetVolume) {
    auto attributes = ProtoStruct{};
    ResourceConfig config(
        "rdk:component:speaker", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    ProtoStruct command{{"set_volume", 75.0}};
    auto result = speaker.do_command(command);

    ASSERT_TRUE(result.count("volume"));
    EXPECT_EQ(*result.at("volume").get<double>(), 75.0);
    EXPECT_EQ(speaker.volume_, 75);
}

TEST_F(SpeakerTest, DoCommandSetVolumeInvalidType) {
    auto attributes = ProtoStruct{};
    ResourceConfig config(
        "rdk:component:speaker", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    ProtoStruct command{{"set_volume", "loud"}};
    EXPECT_THROW(speaker.do_command(command), std::invalid_argument);
}

TEST_F(SpeakerTest, DoCommandSetVolumeOutOfRange) {
    auto attributes = ProtoStruct{};
    ResourceConfig config(
        "rdk:component:speaker", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    ProtoStruct command_high{{"set_volume", 101.0}};
    EXPECT_THROW(speaker.do_command(command_high), std::invalid_argument);

    ProtoStruct command_low{{"set_volume", -1.0}};
    EXPECT_THROW(speaker.do_command(command_low), std::invalid_argument);
}


TEST_F(SpeakerTest, DoCommandStop) {
    auto attributes = ProtoStruct{};
    ResourceConfig config(
        "rdk:component:speaker", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    ProtoStruct command{{"stop", true}};
    auto result = speaker.do_command(command);


    ASSERT_TRUE(result.count("stopped"));
    EXPECT_EQ(speaker.stop_requested_, true);
}


TEST_F(SpeakerTest, DoCommandUnknown) {
    auto attributes = ProtoStruct{};
    ResourceConfig config(
        "rdk:component:speaker", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    ProtoStruct command{{"unknown_command", 1.0}};
    EXPECT_THROW(speaker.do_command(command), std::invalid_argument);
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
        speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};

    speaker::Speaker speaker(deps, config, mock_pa_.get());

    ProtoStruct extra{};
    auto props = speaker.get_properties(extra);

    EXPECT_EQ(props.sample_rate_hz, sample_rate);
    EXPECT_EQ(props.num_channels, num_channels);
    ASSERT_EQ(props.supported_codecs.size(), 4);
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
    speaker::Speaker::model,
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
    speaker::Speaker::model,
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
    speaker::Speaker::model,
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
    speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    std::vector<uint8_t> audio_data;

    ProtoStruct extra{};
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, num_channels};

    // Playing empty data should work (just does nothing)
    EXPECT_NO_THROW({
        speaker.play(audio_data, info, extra);
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

// Codec conversion tests - verify play() correctly decodes different formats

TEST_F(SpeakerTest, CodecConversion_PCM16) {
    int sample_rate = 48000;
    int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
    speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Create test PCM16 samples
    int num_samples = 100;
    std::vector<int16_t> test_samples(num_samples);
    for (int i = 0; i < num_samples; i++) {
        test_samples[i] = static_cast<int16_t>(i * 100);
    }

    // Convert to byte array
    std::vector<uint8_t> audio_data(num_samples * sizeof(int16_t));
    std::memcpy(audio_data.data(), test_samples.data(), audio_data.size());

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, num_channels};
    ProtoStruct extra{};

    // Set playback position to end so play() returns immediately
    speaker.audio_context_->playback_position.store(num_samples);

    EXPECT_NO_THROW({
        speaker.play(audio_data, info, extra);
    });

    // Verify samples were written correctly to the buffer
    std::vector<int16_t> read_buffer(num_samples);
    uint64_t read_pos = 0;
    int samples_read = speaker.audio_context_->read_samples(read_buffer.data(), num_samples, read_pos);

    EXPECT_EQ(samples_read, num_samples);
    for (int i = 0; i < num_samples; i++) {
        EXPECT_EQ(read_buffer[i], test_samples[i]);
    }
}

TEST_F(SpeakerTest, CodecConversion_PCM32) {
    int sample_rate = 48000;
    int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
    speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Create test PCM16 samples
    int num_samples = 100;
    std::vector<int16_t> test_samples(num_samples);
    for (int i = 0; i < num_samples; i++) {
        test_samples[i] = static_cast<int16_t>(i * 100);
    }

    // Convert to PCM32 format
    std::vector<uint8_t> pcm32_data;
    audio::codec::convert_pcm16_to_pcm32(test_samples.data(), num_samples, pcm32_data);

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_32, sample_rate, num_channels};
    ProtoStruct extra{};

    // Set playback position to end so play() returns immediately
    speaker.audio_context_->playback_position.store(num_samples);

    EXPECT_NO_THROW({
        speaker.play(pcm32_data, info, extra);
    });

    // Verify samples were decoded correctly and written to the buffer
    std::vector<int16_t> read_buffer(num_samples);
    uint64_t read_pos = 0;
    int samples_read = speaker.audio_context_->read_samples(read_buffer.data(), num_samples, read_pos);

    EXPECT_EQ(samples_read, num_samples);
    for (int i = 0; i < num_samples; i++) {
        EXPECT_EQ(read_buffer[i], test_samples[i]);
    }
}

TEST_F(SpeakerTest, CodecConversion_PCM32Float) {
    int sample_rate = 48000;
    int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
    speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Create test PCM16 samples
    int num_samples = 100;
    std::vector<int16_t> test_samples(num_samples);
    for (int i = 0; i < num_samples; i++) {
        test_samples[i] = static_cast<int16_t>(i * 100);
    }

    // Convert to PCM32_FLOAT format
    std::vector<uint8_t> float32_data;
    audio::codec::convert_pcm16_to_float32(test_samples.data(), num_samples, float32_data);

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_32_FLOAT, sample_rate, num_channels};
    ProtoStruct extra{};

    // Set playback position to end so play() returns immediately
    speaker.audio_context_->playback_position.store(num_samples);

    EXPECT_NO_THROW({
        speaker.play(float32_data, info, extra);
    });

    // Verify samples were decoded correctly and written to the buffer
    // Allow small rounding errors due to float conversion
    std::vector<int16_t> read_buffer(num_samples);
    uint64_t read_pos = 0;
    int samples_read = speaker.audio_context_->read_samples(read_buffer.data(), num_samples, read_pos);

    EXPECT_EQ(samples_read, num_samples);
    for (int i = 0; i < num_samples; i++) {
        EXPECT_NEAR(read_buffer[i], test_samples[i], 1);
    }
}


TEST_F(SpeakerTest, CodecConversion_PCM32_InvalidSize) {
    int sample_rate = 48000;
    int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
    speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Create PCM32 data with invalid size (not divisible by 4)
    std::vector<uint8_t> invalid_data = {1, 2, 3, 4, 5};

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_32, sample_rate, num_channels};
    ProtoStruct extra{};

    EXPECT_THROW({
        speaker.play(invalid_data, info, extra);
    }, std::invalid_argument);
}

TEST_F(SpeakerTest, CodecConversion_PCM32Float_InvalidSize) {
    int sample_rate = 48000;
    int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
    speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Create float32 data with invalid size (not divisible by 4)
    std::vector<uint8_t> invalid_data = {1, 2, 3, 4, 5, 6, 7};

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_32_FLOAT, sample_rate, num_channels};
    ProtoStruct extra{};

    EXPECT_THROW({
        speaker.play(invalid_data, info, extra);
    }, std::invalid_argument);
}

TEST_F(SpeakerTest, CodecConversion_SampleRateMismatch) {
    int sample_rate = 48000;
    int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
    speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    std::vector<uint8_t> audio_data(100);

    // Try to play PCM16 audio with different sample rate
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, 44100, num_channels};
    ProtoStruct extra{};

    // Set playback position to end so play() returns immediately
    speaker.audio_context_->playback_position.store(100);

    EXPECT_NO_THROW({
        speaker.play(audio_data, info, extra);
    });
}

TEST_F(SpeakerTest, CodecConversion_ChannelConversion) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
    speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Create stereo audio data (must be even number of samples for stereo)
    const int num_samples = 100;
    std::vector<int16_t> test_samples(num_samples);
    for (int i = 0; i < num_samples; i++) {
        test_samples[i] = static_cast<int16_t>(i % 1000);
    }
    std::vector<uint8_t> audio_data(num_samples * sizeof(int16_t));
    std::memcpy(audio_data.data(), test_samples.data(), audio_data.size());

    // Play stereo PCM16 audio on mono speaker — should convert, not throw
    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, 2};
    ProtoStruct extra{};

    // After stereo→mono conversion: 100 stereo samples become 50 mono samples
    const int expected_samples = num_samples / 2;

    // Set playback position so play() returns immediately after writing
    speaker.audio_context_->playback_position.store(expected_samples);

    EXPECT_NO_THROW(speaker.play(audio_data, info, extra));
}


TEST_F(SpeakerTest, PlayPCM16WithWavHeader) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout",
        "",
        test_name_,
        attributes,
        "",
        speaker::Speaker::model,
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Create test PCM16 samples
    const int num_samples = 100;
    std::vector<int16_t> test_samples(num_samples);
    for (int i = 0; i < num_samples; i++) {
        test_samples[i] = static_cast<int16_t>(i * 100);
    }

    const size_t pcm_size = num_samples * sizeof(int16_t);

    // Build a minimal 44-byte WAV header
    std::vector<uint8_t> audio_data;
    audio_data.resize(44 + pcm_size);

    // RIFF header
    audio_data[0] = 'R'; audio_data[1] = 'I'; audio_data[2] = 'F'; audio_data[3] = 'F';
    // ChunkSize (file size - 8)
    const uint32_t chunk_size = static_cast<uint32_t>(36 + pcm_size);
    std::memcpy(&audio_data[4], &chunk_size, 4);
    // WAVE
    audio_data[8] = 'W'; audio_data[9] = 'A'; audio_data[10] = 'V'; audio_data[11] = 'E';
    // fmt subchunk
    audio_data[12] = 'f'; audio_data[13] = 'm'; audio_data[14] = 't'; audio_data[15] = ' ';
    const uint32_t subchunk1_size = 16;
    std::memcpy(&audio_data[16], &subchunk1_size, 4);
    const uint16_t audio_format = 1; // PCM
    std::memcpy(&audio_data[20], &audio_format, 2);
    const uint16_t wav_channels = num_channels;
    std::memcpy(&audio_data[22], &wav_channels, 2);
    const uint32_t wav_sample_rate = sample_rate;
    std::memcpy(&audio_data[24], &wav_sample_rate, 4);
    const uint32_t byte_rate = sample_rate * num_channels * 2;
    std::memcpy(&audio_data[28], &byte_rate, 4);
    const uint16_t block_align = num_channels * 2;
    std::memcpy(&audio_data[32], &block_align, 2);
    const uint16_t bits_per_sample = 16;
    std::memcpy(&audio_data[34], &bits_per_sample, 2);
    // data subchunk
    audio_data[36] = 'd'; audio_data[37] = 'a'; audio_data[38] = 't'; audio_data[39] = 'a';
    const uint32_t data_size = static_cast<uint32_t>(pcm_size);
    std::memcpy(&audio_data[40], &data_size, 4);

    // Copy PCM samples after header
    std::memcpy(&audio_data[44], test_samples.data(), pcm_size);

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, num_channels};
    ProtoStruct extra{};

    // Set playback position so play() returns immediately
    speaker.audio_context_->playback_position.store(num_samples);

    EXPECT_NO_THROW(speaker.play(audio_data, info, extra));

    // Verify only the PCM samples (not header bytes) were written to the buffer
    std::vector<int16_t> read_buffer(num_samples);
    uint64_t read_pos = 0;
    int samples_read = speaker.audio_context_->read_samples(read_buffer.data(), num_samples, read_pos);

    EXPECT_EQ(samples_read, num_samples);
    for (int i = 0; i < num_samples; i++) {
        EXPECT_EQ(read_buffer[i], test_samples[i]);
    }
}

 TEST_F(SpeakerTest, Play_ResamplesSampleRateMismatch) {
      // Speaker configured for 48000 Hz
      int speaker_sample_rate = 48000;
      int num_channels = 2;

      auto attributes = ProtoStruct{};
      attributes["sample_rate"] = static_cast<double>(speaker_sample_rate);
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

      // Create audio at 44100 Hz (different from speaker's 48000 Hz)
      int audio_sample_rate = 44100;
      int duration_ms = 100;
      int num_samples = (audio_sample_rate * duration_ms / 1000) * num_channels;

      std::vector<int16_t> test_samples(num_samples);
      for (int i = 0; i < num_samples; i++) {
          test_samples[i] = static_cast<int16_t>(i % 1000);
      }

      std::vector<uint8_t> audio_data(num_samples * sizeof(int16_t));
      std::memcpy(audio_data.data(), test_samples.data(), audio_data.size());

      viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, audio_sample_rate, num_channels};
      ProtoStruct extra{};

      // Calculate expected number of samples after resampling
      // resampled_samples = original_samples * (speaker_rate / audio_rate)
      int expected_resampled_samples = (num_samples * speaker_sample_rate) / audio_sample_rate;

      // Set playback position so play() returns immediately after writing
      speaker.audio_context_->playback_position.store(expected_resampled_samples);

      EXPECT_NO_THROW({
          speaker.play(audio_data, info, extra);
      });

      // Verify that resampled audio was written to buffer
      // The buffer should have approximately the expected number of resampled samples
      uint64_t write_pos = speaker.audio_context_->get_write_position();

      EXPECT_EQ(write_pos, expected_resampled_samples);

      // Verify we can read back resampled data
      std::vector<int16_t> read_buffer(expected_resampled_samples);
      uint64_t read_pos = 0;
      int samples_read = speaker.audio_context_->read_samples(
          read_buffer.data(), expected_resampled_samples, read_pos);

      EXPECT_GT(samples_read, 0);
      EXPECT_EQ(samples_read, expected_resampled_samples);
  }


// play_stream tests

TEST_F(SpeakerTest, PlayStream_RejectsMP3) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    viam::sdk::audio_info info{viam::sdk::audio_codecs::MP3, sample_rate, num_channels};
    ProtoStruct extra{};

    auto chunk_source = []() -> boost::optional<std::vector<uint8_t>> {
        return boost::none;
    };

    EXPECT_THROW(speaker.play_stream(info, chunk_source, extra), std::invalid_argument);
}

TEST_F(SpeakerTest, PlayStream_EmptySourceCompletes) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, num_channels};
    ProtoStruct extra{};

    auto chunk_source = []() -> boost::optional<std::vector<uint8_t>> {
        return boost::none;
    };

    EXPECT_NO_THROW(speaker.play_stream(info, chunk_source, extra));
    EXPECT_EQ(speaker.audio_context_->get_write_position(), 0u);
}

TEST_F(SpeakerTest, PlayStream_PCM16_MultipleChunks) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    const int samples_per_chunk = 100;
    const int num_chunks = 3;
    const int total_samples = samples_per_chunk * num_chunks;

    std::vector<int16_t> expected(total_samples);
    for (int i = 0; i < total_samples; i++) {
        expected[i] = static_cast<int16_t>(i + 1);
    }

    int chunk_index = 0;
    auto chunk_source = [&]() -> boost::optional<std::vector<uint8_t>> {
        if (chunk_index >= num_chunks) {
            return boost::none;
        }
        std::vector<uint8_t> chunk(samples_per_chunk * sizeof(int16_t));
        std::memcpy(chunk.data(),
                    expected.data() + chunk_index * samples_per_chunk,
                    chunk.size());
        chunk_index++;
        return chunk;
    };

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, num_channels};
    ProtoStruct extra{};

    // Pre-advance playback position so wait_for_playback exits immediately once all chunks land.
    speaker.audio_context_->playback_position.store(total_samples);

    EXPECT_NO_THROW(speaker.play_stream(info, chunk_source, extra));
    EXPECT_EQ(chunk_index, num_chunks);
    EXPECT_EQ(speaker.audio_context_->get_write_position(), static_cast<uint64_t>(total_samples));

    std::vector<int16_t> read_buffer(total_samples);
    uint64_t read_pos = 0;
    int samples_read = speaker.audio_context_->read_samples(read_buffer.data(), total_samples, read_pos);
    EXPECT_EQ(samples_read, total_samples);
    for (int i = 0; i < total_samples; i++) {
        EXPECT_EQ(read_buffer[i], expected[i]);
    }
}

TEST_F(SpeakerTest, PlayStream_PCM32_DecodesChunk) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    const int num_samples = 100;
    std::vector<int16_t> expected(num_samples);
    for (int i = 0; i < num_samples; i++) {
        expected[i] = static_cast<int16_t>(i * 100);
    }

    std::vector<uint8_t> pcm32_chunk;
    audio::codec::convert_pcm16_to_pcm32(expected.data(), num_samples, pcm32_chunk);

    bool delivered = false;
    auto chunk_source = [&]() -> boost::optional<std::vector<uint8_t>> {
        if (delivered) {
            return boost::none;
        }
        delivered = true;
        return pcm32_chunk;
    };

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_32, sample_rate, num_channels};
    ProtoStruct extra{};

    speaker.audio_context_->playback_position.store(num_samples);

    EXPECT_NO_THROW(speaker.play_stream(info, chunk_source, extra));

    std::vector<int16_t> read_buffer(num_samples);
    uint64_t read_pos = 0;
    int samples_read = speaker.audio_context_->read_samples(read_buffer.data(), num_samples, read_pos);
    EXPECT_EQ(samples_read, num_samples);
    for (int i = 0; i < num_samples; i++) {
        EXPECT_EQ(read_buffer[i], expected[i]);
    }
}

TEST_F(SpeakerTest, PlayStream_PCM32Float_DecodesChunk) {
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    const int num_samples = 100;
    std::vector<int16_t> expected(num_samples);
    for (int i = 0; i < num_samples; i++) {
        expected[i] = static_cast<int16_t>(i * 100);
    }

    std::vector<uint8_t> float_chunk;
    audio::codec::convert_pcm16_to_float32(expected.data(), num_samples, float_chunk);

    bool delivered = false;
    auto chunk_source = [&]() -> boost::optional<std::vector<uint8_t>> {
        if (delivered) {
            return boost::none;
        }
        delivered = true;
        return float_chunk;
    };

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_32_FLOAT, sample_rate, num_channels};
    ProtoStruct extra{};

    speaker.audio_context_->playback_position.store(num_samples);

    EXPECT_NO_THROW(speaker.play_stream(info, chunk_source, extra));

    std::vector<int16_t> read_buffer(num_samples);
    uint64_t read_pos = 0;
    int samples_read = speaker.audio_context_->read_samples(read_buffer.data(), num_samples, read_pos);
    EXPECT_EQ(samples_read, num_samples);
    for (int i = 0; i < num_samples; i++) {
        EXPECT_NEAR(read_buffer[i], expected[i], 1);
    }
}

TEST_F(SpeakerTest, PlayStream_ChannelConversion) {
    // Speaker is mono, chunks come in as stereo PCM16 — process_and_write_pcm should downmix.
    const int sample_rate = 48000;
    const int speaker_channels = 1;
    const int audio_channels = 2;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(speaker_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    const int stereo_samples = 100;
    std::vector<int16_t> stereo(stereo_samples);
    for (int i = 0; i < stereo_samples; i++) {
        stereo[i] = static_cast<int16_t>(i % 1000);
    }
    std::vector<uint8_t> chunk(stereo_samples * sizeof(int16_t));
    std::memcpy(chunk.data(), stereo.data(), chunk.size());

    bool delivered = false;
    auto chunk_source = [&]() -> boost::optional<std::vector<uint8_t>> {
        if (delivered) {
            return boost::none;
        }
        delivered = true;
        return chunk;
    };

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, audio_channels};
    ProtoStruct extra{};

    const int expected_mono_samples = stereo_samples / 2;
    speaker.audio_context_->playback_position.store(expected_mono_samples);

    EXPECT_NO_THROW(speaker.play_stream(info, chunk_source, extra));
    EXPECT_EQ(speaker.audio_context_->get_write_position(), static_cast<uint64_t>(expected_mono_samples));
}

TEST_F(SpeakerTest, PlayStream_Resamples) {
    const int speaker_rate = 48000;
    const int audio_rate = 44100;
    const int num_channels = 2;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(speaker_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    const int duration_ms = 50;
    const int num_samples = (audio_rate * duration_ms / 1000) * num_channels;
    std::vector<int16_t> samples(num_samples);
    for (int i = 0; i < num_samples; i++) {
        samples[i] = static_cast<int16_t>(i % 1000);
    }
    std::vector<uint8_t> chunk(num_samples * sizeof(int16_t));
    std::memcpy(chunk.data(), samples.data(), chunk.size());

    bool delivered = false;
    auto chunk_source = [&]() -> boost::optional<std::vector<uint8_t>> {
        if (delivered) {
            return boost::none;
        }
        delivered = true;
        return chunk;
    };

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, audio_rate, num_channels};
    ProtoStruct extra{};

    const int expected_resampled = (num_samples * speaker_rate) / audio_rate;
    speaker.audio_context_->playback_position.store(expected_resampled);

    EXPECT_NO_THROW(speaker.play_stream(info, chunk_source, extra));
    EXPECT_EQ(speaker.audio_context_->get_write_position(), static_cast<uint64_t>(expected_resampled));
}

TEST_F(SpeakerTest, PlayStream_StopRequestedBreaksLoop) {
    // Verify a mid-stream stop_requested causes the loop to exit after the current chunk
    // without consuming the remaining chunks.
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    const int samples_per_chunk = 100;
    std::vector<int16_t> chunk_samples(samples_per_chunk);
    for (int i = 0; i < samples_per_chunk; i++) {
        chunk_samples[i] = static_cast<int16_t>(i);
    }
    std::vector<uint8_t> chunk(samples_per_chunk * sizeof(int16_t));
    std::memcpy(chunk.data(), chunk_samples.data(), chunk.size());

    int chunks_pulled = 0;
    auto chunk_source = [&]() -> boost::optional<std::vector<uint8_t>> {
        chunks_pulled++;
        if (chunks_pulled == 1) {
            return chunk;
        }
        // After delivering the first chunk, request a stop. The loop should break
        // on the next iteration before processing this second chunk.
        speaker.stop_requested_.store(true);
        return chunk;
    };

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, num_channels};
    ProtoStruct extra{};

    // Only the first chunk should be written before stop interrupts.
    speaker.audio_context_->playback_position.store(samples_per_chunk);

    EXPECT_NO_THROW(speaker.play_stream(info, chunk_source, extra));
    EXPECT_EQ(chunks_pulled, 2);
    EXPECT_EQ(speaker.audio_context_->get_write_position(), static_cast<uint64_t>(samples_per_chunk));
}

TEST_F(SpeakerTest, PlayStream_ResetsStopRequestedOnEntry) {
    // play_stream() must clear a previously-set stop_requested_ so a prior stop command
    // doesn't poison the next stream.
    const int sample_rate = 48000;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());
    speaker.stop_requested_.store(true);

    const int samples_per_chunk = 50;
    std::vector<int16_t> chunk_samples(samples_per_chunk);
    for (int i = 0; i < samples_per_chunk; i++) {
        chunk_samples[i] = static_cast<int16_t>(i + 1);
    }
    std::vector<uint8_t> chunk(samples_per_chunk * sizeof(int16_t));
    std::memcpy(chunk.data(), chunk_samples.data(), chunk.size());

    bool delivered = false;
    auto chunk_source = [&]() -> boost::optional<std::vector<uint8_t>> {
        if (delivered) {
            return boost::none;
        }
        delivered = true;
        return chunk;
    };

    viam::sdk::audio_info info{viam::sdk::audio_codecs::PCM_16, sample_rate, num_channels};
    ProtoStruct extra{};

    speaker.audio_context_->playback_position.store(samples_per_chunk);

    EXPECT_NO_THROW(speaker.play_stream(info, chunk_source, extra));
    EXPECT_EQ(speaker.audio_context_->get_write_position(), static_cast<uint64_t>(samples_per_chunk));
}


// Watchdog: when the speaker callback stops firing, the background watcher should
// detect the staleness on its next poll and replace audio_context_ with a fresh one.
// We force the stale state by manually setting last_callback_time_ns to a timestamp
// well past STREAM_RESTART_THRESHOLD_MS in the past, then sleep one poll cycle + slack.
TEST_F(SpeakerTest, WatchdogRestartsStalledStream) {
    const int sample_rate = 44100;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    // Snapshot the audio context the watcher will see initially.
    std::shared_ptr<audio::OutputStreamContext> initial_context;
    {
        std::lock_guard<std::mutex> lock(speaker.stream_mu_);
        initial_context = speaker.audio_context_;
    }
    ASSERT_TRUE(initial_context);

    // Pretend the callback last fired 5 seconds ago — well past the 2s threshold.
    test_utils::mark_callback_stale(initial_context);

    // Wait long enough for the watchdog to wake, see the stale timestamp,
    // and run restart_stalled_stream.
    test_utils::wait_one_poll();

    std::shared_ptr<audio::OutputStreamContext> after_context;
    int after_attempts = -1;
    {
        std::lock_guard<std::mutex> lock(speaker.stream_mu_);
        after_context = speaker.audio_context_;
        after_attempts = speaker.restart_attempts_;
    }

    EXPECT_NE(after_context.get(), initial_context.get())
        << "watchdog should have replaced audio_context_ after detecting stall";
    EXPECT_EQ(after_attempts, 0)
        << "restart should have succeeded with the mock pa returning paNoError";
}

// Watchdog: if restart_stream fails (e.g. PortAudio errors), restart_attempts_ should
// climb instead of resetting to 0. The fresh audio_context_ is also NOT swapped in
// because restart_stalled_stream only updates audio_context_ on success.
TEST_F(SpeakerTest, WatchdogIncrementsAttemptsOnRestartFailure) {
    using ::testing::_;
    using ::testing::Return;

    const int sample_rate = 44100;
    const int num_channels = 1;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioout", "", test_name_, attributes, "",
        speaker::Speaker::model, LinkConfig{}, log_level::info);

    Dependencies deps{};
    speaker::Speaker speaker(deps, config, mock_pa_.get());

    std::shared_ptr<audio::OutputStreamContext> initial_context;
    {
        std::lock_guard<std::mutex> lock(speaker.stream_mu_);
        initial_context = speaker.audio_context_;
    }
    ASSERT_TRUE(initial_context);

    // Make any future startStream call fail. The initial stream is already up;
    // this only affects the watcher's restart attempt.
    EXPECT_CALL(*mock_pa_, startStream(_)).WillRepeatedly(Return(paInternalError));

    test_utils::mark_callback_stale(initial_context);

    test_utils::wait_one_poll();

    std::shared_ptr<audio::OutputStreamContext> after_context;
    int after_attempts = -1;
    {
        std::lock_guard<std::mutex> lock(speaker.stream_mu_);
        after_context = speaker.audio_context_;
        after_attempts = speaker.restart_attempts_;
    }

    EXPECT_GE(after_attempts, 1) << "failed restart should have bumped restart_attempts_";
    EXPECT_EQ(after_context.get(), initial_context.get())
        << "audio_context_ should not be swapped on failed restart";
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
  return RUN_ALL_TESTS();
}
