#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/config/resource.hpp>
#include "audio_utils.hpp"
#include "audio_stream.hpp"
#include "test_utils.hpp"

using namespace viam::sdk;
using namespace audio;


class AudioUtilsTest : public test_utils::AudioTestBase {
protected:
    void SetUp() override {
        AudioTestBase::SetUp();
    }
};

TEST_F(AudioUtilsTest, ParseConfigAttributesEmpty) {
    auto attributes = ProtoStruct{};
    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "");
    EXPECT_FALSE(params.sample_rate.has_value());
    EXPECT_FALSE(params.num_channels.has_value());
    EXPECT_FALSE(params.latency_ms.has_value());
}

TEST_F(AudioUtilsTest, ParseConfigAttributesDeviceName) {
    auto attributes = ProtoStruct{};
    attributes["device_name"] = std::string("Test Device");

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "Test Device");
    EXPECT_FALSE(params.sample_rate.has_value());
    EXPECT_FALSE(params.num_channels.has_value());
    EXPECT_FALSE(params.latency_ms.has_value());
}

TEST_F(AudioUtilsTest, ParseConfigAttributesSampleRate) {
    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = 48000.0;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "");
    ASSERT_TRUE(params.sample_rate.has_value());
    EXPECT_EQ(params.sample_rate.value(), 48000);
    EXPECT_FALSE(params.num_channels.has_value());
    EXPECT_FALSE(params.latency_ms.has_value());
}

TEST_F(AudioUtilsTest, ParseConfigAttributesNumChannels) {
    auto attributes = ProtoStruct{};
    attributes["num_channels"] = 2.0;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "");
    EXPECT_FALSE(params.sample_rate.has_value());
    ASSERT_TRUE(params.num_channels.has_value());
    EXPECT_EQ(params.num_channels.value(), 2);
    EXPECT_FALSE(params.latency_ms.has_value());
}

TEST_F(AudioUtilsTest, ParseConfigAttributesLatency) {
    auto attributes = ProtoStruct{};
    attributes["latency"] = 100.0;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "");
    EXPECT_FALSE(params.sample_rate.has_value());
    EXPECT_FALSE(params.num_channels.has_value());
    ASSERT_TRUE(params.latency_ms.has_value());
    EXPECT_EQ(params.latency_ms.value(), 100.0);
}

TEST_F(AudioUtilsTest, ParseConfigAttributesAll) {
    auto attributes = ProtoStruct{};
    attributes["device_name"] = std::string("My Device");
    attributes["sample_rate"] = 44100.0;
    attributes["num_channels"] = 1.0;
    attributes["latency"] = 50.0;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto params = audio::utils::parseConfigAttributes(config);

    EXPECT_EQ(params.device_name, "My Device");
    ASSERT_TRUE(params.sample_rate.has_value());
    EXPECT_EQ(params.sample_rate.value(), 44100);
    ASSERT_TRUE(params.num_channels.has_value());
    EXPECT_EQ(params.num_channels.value(), 1);
    ASSERT_TRUE(params.latency_ms.has_value());
    EXPECT_EQ(params.latency_ms.value(), 50.0);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigUsesDefaults) {
    using ::testing::Return;

    audio::utils::ConfigParams params;
    params.device_name = "";  // Use default device

    auto stream_params = audio::utils::setupStreamFromConfig(
        params,
        audio::utils::StreamDirection::Input,
        nullptr,
        mock_pa_.get()
    );

    EXPECT_EQ(stream_params.device_index, 0);
    EXPECT_EQ(stream_params.device_name, test_utils::AudioTestBase::testDeviceName);
    EXPECT_EQ(stream_params.sample_rate, 44100);
    EXPECT_EQ(stream_params.num_channels, 1);
    EXPECT_TRUE(stream_params.is_input);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigUsesProvidedValues) {
    audio::utils::ConfigParams params;
    params.device_name = "";
    params.sample_rate = 48000;
    params.num_channels = 2;
    params.latency_ms = 100.0;

    auto stream_params = audio::utils::setupStreamFromConfig(
        params,
        audio::utils::StreamDirection::Input,
        nullptr,
        mock_pa_.get()
    );

    EXPECT_EQ(stream_params.sample_rate, 48000);  // natively supported by mock, so used directly
    EXPECT_EQ(stream_params.num_channels, 2);
    EXPECT_DOUBLE_EQ(stream_params.suggested_latency_seconds, 0.1);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigInputUsesSampleRateWhenNativelySupported) {
    using ::testing::Return;
    ON_CALL(*mock_pa_, isFormatSupported(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(Return(paNoError));

    audio::utils::ConfigParams params;
    params.device_name = "";
    params.sample_rate = 48000;

    auto stream_params = audio::utils::setupStreamFromConfig(
        params,
        audio::utils::StreamDirection::Input,
        nullptr,
        mock_pa_.get()
    );

    EXPECT_EQ(stream_params.sample_rate, 48000);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigInputFallsBackToDeviceDefaultWhenSampleRateNotSupported) {
    using ::testing::Return;
    ON_CALL(*mock_pa_, isFormatSupported(::testing::_, ::testing::_, ::testing::_))
        .WillByDefault(Return(paInvalidSampleRate));

    audio::utils::ConfigParams params;
    params.device_name = "";
    params.sample_rate = 48000;

    auto stream_params = audio::utils::setupStreamFromConfig(
        params,
        audio::utils::StreamDirection::Input,
        nullptr,
        mock_pa_.get()
    );

    EXPECT_EQ(stream_params.sample_rate, test_utils::DEFAULT_DEVICE_SAMPLE_RATE);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigOutputDirection) {
    using ::testing::Return;

    ON_CALL(*mock_pa_, getDefaultOutputDevice())
        .WillByDefault(Return(0));

    audio::utils::ConfigParams params;
    params.device_name = "";

    auto stream_params = audio::utils::setupStreamFromConfig(
        params,
        audio::utils::StreamDirection::Output,
        nullptr,
        mock_pa_.get()
    );

    EXPECT_FALSE(stream_params.is_input);
}

TEST_F(AudioUtilsTest, SetupStreamFromConfigThrowsOnExcessiveChannels) {
    audio::utils::ConfigParams params;
    params.device_name = "";
    params.num_channels = 10;  // More than mock_device_info_.maxInputChannels (2)

    EXPECT_THROW({
        audio::utils::setupStreamFromConfig(
            params,
            audio::utils::StreamDirection::Input,
            nullptr,
            mock_pa_.get()
        );
    }, std::invalid_argument);
}

TEST_F(AudioUtilsTest, FindDeviceByNameFindsDevice) {
    using ::testing::Return;

    PaDeviceInfo device1;
    device1.name = "Device 1";
    PaDeviceInfo device2;
    device2.name = "Device 2";

    ON_CALL(*mock_pa_, getDeviceCount())
        .WillByDefault(Return(2));

    EXPECT_CALL(*mock_pa_, getDeviceInfo(0))
        .WillRepeatedly(Return(&device1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(1))
        .WillRepeatedly(Return(&device2));

    PaDeviceIndex idx = audio::utils::findDeviceByName("Device 2", *mock_pa_);
    EXPECT_EQ(idx, 1);
}

TEST_F(AudioUtilsTest, FindDeviceByNameReturnsNoDeviceWhenNotFound) {
    using ::testing::Return;

    PaDeviceInfo device1;
    device1.name = "Device 1";

    ON_CALL(*mock_pa_, getDeviceCount())
        .WillByDefault(Return(1));

    EXPECT_CALL(*mock_pa_, getDeviceInfo(0))
        .WillRepeatedly(Return(&device1));

    PaDeviceIndex idx = audio::utils::findDeviceByName("Nonexistent", *mock_pa_);
    EXPECT_EQ(idx, paNoDevice);
}

// Mock callback for testing
int testInputCallback(const void*, void*, unsigned long, const PaStreamCallbackTimeInfo*,
                      PaStreamCallbackFlags, void*) {
    return paContinue;
}

int testOutputCallback(const void*, void*, unsigned long, const PaStreamCallbackTimeInfo*,
                       PaStreamCallbackFlags, void*) {
    return paContinue;
}

TEST_F(AudioUtilsTest, SetupAudioDeviceInputStreamContext) {
    PaDeviceInfo device1;
    device1.name = "Test Input Device";
    device1.maxInputChannels = 2;
    device1.maxOutputChannels = 0;
    device1.defaultLowInputLatency = 0.01;

    ON_CALL(*mock_pa_, getDeviceCount()).WillByDefault(::testing::Return(1));
    ON_CALL(*mock_pa_, getDeviceInfo(0)).WillByDefault(::testing::Return(&device1));
    ON_CALL(*mock_pa_, getDefaultInputDevice()).WillByDefault(::testing::Return(0));
    ON_CALL(*mock_pa_, isFormatSupported(testing::_, testing::_, testing::_))
        .WillByDefault(::testing::Return(paNoError));

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = 48000;
    attributes["num_channels"] = 2;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    // Call the helper function
    auto setup = audio::utils::setup_audio_device<audio::InputStreamContext>(
        config,
        audio::utils::StreamDirection::Input,
        testInputCallback,
        mock_pa_.get(),
        30
    );
    EXPECT_EQ(setup.stream_params.sample_rate, 48000);
    EXPECT_EQ(setup.stream_params.num_channels, 2);
    EXPECT_EQ(setup.stream_params.callback, testInputCallback);
    EXPECT_TRUE(setup.stream_params.is_input);
    EXPECT_NE(setup.audio_context, nullptr);
    EXPECT_EQ(setup.stream_params.user_data, setup.audio_context.get());

    EXPECT_EQ(setup.audio_context->info.sample_rate_hz, 48000);
    EXPECT_EQ(setup.audio_context->info.num_channels, 2);
    EXPECT_EQ(setup.audio_context->info.codec, viam::sdk::audio_codecs::PCM_16);
}

TEST_F(AudioUtilsTest, SetupAudioDeviceOutputStreamContext) {
    // Setup mock PortAudio expectations
    PaDeviceInfo device1;
    device1.name = "Test Output Device";
    device1.maxInputChannels = 0;
    device1.maxOutputChannels = 2;
    device1.defaultSampleRate = 44100.0;
    device1.defaultLowOutputLatency = 0.01;

    ON_CALL(*mock_pa_, getDeviceCount()).WillByDefault(::testing::Return(1));
    ON_CALL(*mock_pa_, getDeviceInfo(0)).WillByDefault(::testing::Return(&device1));
    ON_CALL(*mock_pa_, getDefaultOutputDevice()).WillByDefault(::testing::Return(0));
    ON_CALL(*mock_pa_, isFormatSupported(testing::_, testing::_, testing::_))
        .WillByDefault(::testing::Return(paNoError));

    auto attributes = ProtoStruct{};
    attributes["device_name"] = std::string("Test Output Device");

    ResourceConfig config(
        "rdk:component:audioout", "", "test", attributes, "",
        Model("viam", "audio", "speaker"), LinkConfig{}, log_level::info
    );

    auto setup = audio::utils::setup_audio_device<audio::OutputStreamContext>(
        config,
        audio::utils::StreamDirection::Output,
        testOutputCallback,
        mock_pa_.get(),
        30
    );

    EXPECT_EQ(setup.stream_params.device_name, "Test Output Device");
    EXPECT_EQ(setup.stream_params.callback, testOutputCallback);
    EXPECT_FALSE(setup.stream_params.is_input);
    EXPECT_NE(setup.audio_context, nullptr);
    EXPECT_EQ(setup.stream_params.user_data, setup.audio_context.get());

    EXPECT_EQ(setup.audio_context->info.codec, viam::sdk::audio_codecs::PCM_16);
}

TEST_F(AudioUtilsTest, SetupAudioDeviceUsesConfigParams) {
    // Setup mock
    PaDeviceInfo device1;
    device1.name = "My Device";
    device1.maxInputChannels = 2;
    device1.defaultSampleRate = 44100.0;
    device1.defaultLowInputLatency = 0.01;

    ON_CALL(*mock_pa_, getDeviceCount()).WillByDefault(::testing::Return(1));
    ON_CALL(*mock_pa_, getDeviceInfo(0)).WillByDefault(::testing::Return(&device1));
    ON_CALL(*mock_pa_, isFormatSupported(testing::_, testing::_, testing::_))
        .WillByDefault(::testing::Return(paNoError));

    auto attributes = ProtoStruct{};
    attributes["device_name"] = std::string("My Device");
    attributes["historical_throttle_ms"] = 100;

    ResourceConfig config(
        "rdk:component:audioin", "", "test", attributes, "",
        Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
    );

    auto setup = audio::utils::setup_audio_device<audio::InputStreamContext>(
        config,
        audio::utils::StreamDirection::Input,
        testInputCallback,
        mock_pa_.get()
    );

    EXPECT_TRUE(setup.config_params.historical_throttle_ms.has_value());
    EXPECT_EQ(setup.config_params.historical_throttle_ms.value(), 100);
    EXPECT_EQ(setup.config_params.device_name, "My Device");
}

TEST_F(AudioUtilsTest, GetStreamLatencyFallsBackWhenStreamInfoNull) {
    using ::testing::Return;
    ON_CALL(*mock_pa_, getStreamInfo(::testing::_)).WillByDefault(Return(nullptr));

    audio::utils::StreamParams params;
    params.suggested_latency_seconds = 0.05;
    params.is_input = false;

    double latency = audio::utils::get_stream_latency(nullptr, params, mock_pa_.get());
    EXPECT_DOUBLE_EQ(latency, 0.05);
}

TEST_F(AudioUtilsTest, GetStreamLatencyReturnsOutputLatency) {
    using ::testing::Return;
    PaStreamInfo stream_info;
    stream_info.inputLatency  = 0.02;
    stream_info.outputLatency = 0.04;
    ON_CALL(*mock_pa_, getStreamInfo(::testing::_)).WillByDefault(Return(&stream_info));

    audio::utils::StreamParams params;
    params.suggested_latency_seconds = 0.01;
    params.is_input = false;

    double latency = audio::utils::get_stream_latency(nullptr, params, mock_pa_.get());
    EXPECT_DOUBLE_EQ(latency, 0.04);
}

TEST_F(AudioUtilsTest, GetStreamLatencyReturnsInputLatency) {
    using ::testing::Return;
    PaStreamInfo stream_info;
    stream_info.inputLatency  = 0.02;
    stream_info.outputLatency = 0.04;
    ON_CALL(*mock_pa_, getStreamInfo(::testing::_)).WillByDefault(Return(&stream_info));

    audio::utils::StreamParams params;
    params.suggested_latency_seconds = 0.01;
    params.is_input = true;

    double latency = audio::utils::get_stream_latency(nullptr, params, mock_pa_.get());
    EXPECT_DOUBLE_EQ(latency, 0.02);
}

TEST_F(AudioUtilsTest, AbortStream_Success) {
    using ::testing::Return;
    EXPECT_CALL(*mock_pa_, abortStream(::testing::_)).WillOnce(Return(paNoError));
    EXPECT_CALL(*mock_pa_, closeStream(::testing::_)).WillOnce(Return(paNoError));

    EXPECT_NO_THROW(audio::utils::abort_stream(nullptr, mock_pa_.get()));
}

TEST_F(AudioUtilsTest, AbortStream_ThrowsOnAbortFailure) {
    using ::testing::Return;
    EXPECT_CALL(*mock_pa_, abortStream(::testing::_)).WillOnce(Return(paInternalError));
    EXPECT_CALL(*mock_pa_, closeStream(::testing::_)).Times(0);

    EXPECT_THROW(audio::utils::abort_stream(nullptr, mock_pa_.get()), std::runtime_error);
}

TEST_F(AudioUtilsTest, AbortStream_ThrowsOnCloseFailure) {
    using ::testing::Return;
    EXPECT_CALL(*mock_pa_, abortStream(::testing::_)).WillOnce(Return(paNoError));
    EXPECT_CALL(*mock_pa_, closeStream(::testing::_)).WillOnce(Return(paInternalError));

    EXPECT_THROW(audio::utils::abort_stream(nullptr, mock_pa_.get()), std::runtime_error);
}

// --- resolve_device_id_into_params -------------------------------------------------

// File-scope helpers shared by the four resolver tests below.
namespace {

audio::utils::StreamParams params_at(PaDeviceIndex idx, const std::string& name) {
    audio::utils::StreamParams p{};
    p.device_index = idx;
    p.device_name = name;
    return p;
}

PaDeviceInfo make_device_info(const char* name) {
    PaDeviceInfo info{};
    info.name = name;
    return info;
}

// Build a fresh resolver mock and pre-load the "no match by default" behavior the
// resolver tests all want. Returns the unique_ptr so the caller controls lifetime.
std::unique_ptr<::testing::NiceMock<test_utils::MockDeviceIdResolver>> make_resolver_mock() {
    auto resolver = std::make_unique<::testing::NiceMock<test_utils::MockDeviceIdResolver>>();
    ON_CALL(*resolver, resolve(::testing::_, ::testing::_))
        .WillByDefault(::testing::Return(std::string{}));
    return resolver;
}

}  // namespace

TEST_F(AudioUtilsTest, ResolveDeviceId_EmptyDeviceIdIsNoOp) {
    auto resolver = make_resolver_mock();
    auto params = params_at(7, "old name");
    const auto status =
        audio::utils::resolve_device_id_into_params(/*device_id=*/"", params, mock_pa_.get(), "[test]", resolver.get());
    EXPECT_EQ(status, audio::utils::ResolveStatus::NotConfigured);
    EXPECT_EQ(params.device_index, 7);
    EXPECT_EQ(params.device_name, "old name");
}

TEST_F(AudioUtilsTest, ResolveDeviceId_NotFoundLeavesParamsUnchanged) {
    using ::testing::Return;
    using ::testing::_;

    auto resolver = make_resolver_mock();
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(2));
    PaDeviceInfo a = make_device_info("device a");
    PaDeviceInfo b = make_device_info("device b");
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&a));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(1)).WillRepeatedly(Return(&b));
    // Resolver default returns "" for every device, so "looking-for-id" never matches.

    auto params = params_at(0, "device a");
    const auto status =
        audio::utils::resolve_device_id_into_params("looking-for-id", params, mock_pa_.get(), "[test]", resolver.get());
    EXPECT_EQ(status, audio::utils::ResolveStatus::NotFound);
    EXPECT_EQ(params.device_index, 0);
    EXPECT_EQ(params.device_name, "device a");
}

TEST_F(AudioUtilsTest, ResolveDeviceId_AtSameIndexIsNoOp) {
    using ::testing::Return;
    using ::testing::_;

    auto resolver = make_resolver_mock();
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(2));
    PaDeviceInfo a = make_device_info("device a");
    PaDeviceInfo b = make_device_info("device b");
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&a));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(1)).WillRepeatedly(Return(&b));

    // Resolver: index 0 holds "stable-id"; others stay empty (default).
    EXPECT_CALL(*resolver, resolve(0, _)).WillRepeatedly(Return(std::string{"stable-id"}));

    auto params = params_at(0, "device a");
    const auto status =
        audio::utils::resolve_device_id_into_params("stable-id", params, mock_pa_.get(), "[test]", resolver.get());
    EXPECT_EQ(status, audio::utils::ResolveStatus::Found);
    EXPECT_EQ(params.device_index, 0);
    EXPECT_EQ(params.device_name, "device a");
}

TEST_F(AudioUtilsTest, ResolveDeviceId_MovedUpdatesParams) {
    using ::testing::Return;
    using ::testing::_;

    auto resolver = make_resolver_mock();
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(2));
    PaDeviceInfo a = make_device_info("device a");
    PaDeviceInfo b = make_device_info("device b new path");
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&a));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(1)).WillRepeatedly(Return(&b));

    ON_CALL(*resolver, resolve(1, _)).WillByDefault(Return(std::string{"moving-id"}));

    auto params = params_at(0, "device a");
    const auto status =
        audio::utils::resolve_device_id_into_params("moving-id", params, mock_pa_.get(), "[test]", resolver.get());
    EXPECT_EQ(status, audio::utils::ResolveStatus::Found);
    EXPECT_EQ(params.device_index, 1);
    EXPECT_EQ(params.device_name, "device b new path");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
  return RUN_ALL_TESTS();
}
