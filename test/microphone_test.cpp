#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/config/resource.hpp>
#include <portaudio.h>
#include <viam/sdk/common/audio.hpp>
#include "microphone.hpp"
#include <thread>

class MicrophoneTestEnvironment : public ::testing::Environment {
public:
  void SetUp() override { instance_ = std::make_unique<viam::sdk::Instance>(); }

  void TearDown() override { instance_.reset(); }

private:
  std::unique_ptr<viam::sdk::Instance> instance_;
};


using namespace viam::sdk;
using namespace audio;


// Mock PortAudio interface
class MockPortAudio : public audio::portaudio::PortAudioInterface {
public:
      MOCK_METHOD(PaError, initialize, (), (override));
      MOCK_METHOD(PaDeviceIndex, getDefaultInputDevice, (), (override));
      MOCK_METHOD(PaDeviceIndex, getDefaultOutputDevice, (), (override));
      MOCK_METHOD(const PaDeviceInfo*, getDeviceInfo, (PaDeviceIndex device), (override));
      MOCK_METHOD(PaError, openStream, (PaStream** stream, const PaStreamParameters* inputParameters,
                                        const PaStreamParameters* outputParameters, double sampleRate,
                                        unsigned long framesPerBuffer, PaStreamFlags streamFlags,
                                        PaStreamCallback* streamCallback, void* userData), (override));
      MOCK_METHOD(PaError, startStream, (PaStream* stream), (override));
      MOCK_METHOD(PaError, terminate, (), (override));
      MOCK_METHOD(PaError, stopStream, (PaStream* stream), (override));
      MOCK_METHOD(PaError, closeStream, (PaStream* stream), (override));
      MOCK_METHOD(PaDeviceIndex, getDeviceCount, (), (override));
      MOCK_METHOD(PaStreamInfo*, getStreamInfo, (PaStream* stream), (override));
      MOCK_METHOD(PaError, isFormatSupported, (const PaStreamParameters* inputParameters,
                                               const PaStreamParameters* outputParameters,
                                               double sampleRate), (override));
  };

// Base test fixture with common PortAudio mock setup
// All audio tests inherit from this for consistency
class AudioTestBase : public ::testing::Test {
protected:
    void SetUp() override {
        mock_pa_ = std::make_unique<::testing::NiceMock<MockPortAudio>>();

        // Setup mock device info with common defaults
        mock_device_info_.defaultLowInputLatency = 0.01;
        mock_device_info_.defaultLowOutputLatency = 0.01;
        mock_device_info_.defaultSampleRate = 44100.0;
        mock_device_info_.maxInputChannels = 2;
        mock_device_info_.maxOutputChannels = 0;
        mock_device_info_.name = testDeviceName;
    }

    void TearDown() override {
        mock_pa_.reset();
    }

    // Common test device name used across all tests
    static constexpr const char* testDeviceName = "Test Device";

    std::unique_ptr<::testing::NiceMock<MockPortAudio>> mock_pa_;
    PaDeviceInfo mock_device_info_;
};

class MicrophoneTest : public AudioTestBase {
protected:
    void SetUp() override {
        AudioTestBase::SetUp();

        test_mic_name_ = "test_audioin";
        test_name_ = "test_audio";
        test_deps_ = Dependencies{};

        auto attributes = ProtoStruct{};
        test_config_ = std::make_unique<ResourceConfig>(
            "rdk:component:audioin", "", test_name_, attributes, "",
            Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
        );

        SetupDefaultPortAudioBehavior();
    }
    ResourceConfig createConfig(const std::string& device_name = testDeviceName,
                                int sample_rate = 44100,
                                int num_channels = 1,
                                double latency = 0.0) {
        auto attrs = ProtoStruct{};
        if (!device_name.empty()) {
            attrs["device_name"] = device_name;
        }
        attrs["sample_rate"] = static_cast<double>(sample_rate);
        attrs["num_channels"] = static_cast<double>(num_channels);
        if (latency > 0) {
            attrs["latency"] = latency;
        }

        return ResourceConfig(
            "rdk:component:audioin", "", "test_microphone", attrs, "",
            Model("viam", "audio", "microphone"), LinkConfig{}, log_level::info
        );
    }

    // Helper: Setup mock expectations for successful stream creation
    void expectSuccessfulStreamCreation(PaStream* stream_ptr = reinterpret_cast<PaStream*>(0x1234),
                                       int device_index = 0) {
        EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(::testing::Return(device_index + 1));
        EXPECT_CALL(*mock_pa_, getDeviceInfo(device_index)).WillRepeatedly(::testing::Return(&mock_device_info_));
        EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
            .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(stream_ptr), ::testing::Return(paNoError)));
        EXPECT_CALL(*mock_pa_, startStream(::testing::_)).WillOnce(::testing::Return(paNoError));
    }

    // Helper: Create audio context with test data
    std::shared_ptr<microphone::AudioStreamContext> createTestContext(microphone::Microphone& mic,
                                                                       int num_samples = 0) {
        std::shared_ptr<microphone::AudioStreamContext> ctx;
        {
            std::lock_guard<std::mutex> lock(mic.stream_ctx_mu_);
            ctx = mic.audio_context_;
        }

        ctx->first_sample_adc_time = 0.0;
        ctx->stream_start_time = std::chrono::system_clock::now();
        ctx->first_callback_captured.store(true);
        ctx->total_samples_written.store(0);

        // Optionally write samples
        for (int i = 0; i < num_samples; i++) {
            ctx->write_sample(static_cast<int16_t>(i));
        }

        return ctx;
    }

    void SetupDefaultPortAudioBehavior() {
        using ::testing::_;
        using ::testing::Return;


        // setting up mock default behaviors
        ON_CALL(*mock_pa_, getDefaultInputDevice())
            .WillByDefault(Return(0));
        ON_CALL(*mock_pa_, getDeviceInfo(_))
            .WillByDefault(Return(&mock_device_info_));
        ON_CALL(*mock_pa_, getDeviceCount())
            .WillByDefault(Return(1));
        ON_CALL(*mock_pa_, openStream(_, _, _, _, _, _, _, _))
            .WillByDefault(Return(paNoError));
        ON_CALL(*mock_pa_, startStream(_))
            .WillByDefault(Return(paNoError));
        ON_CALL(*mock_pa_, stopStream(_))
            .WillByDefault(Return(paNoError));
        ON_CALL(*mock_pa_, closeStream(_))
            .WillByDefault(Return(paNoError));
        ON_CALL(*mock_pa_, getStreamInfo(_))
            .WillByDefault(Return(nullptr));
        ON_CALL(*mock_pa_, isFormatSupported(_, _, _))
            .WillByDefault(Return(paNoError));
    }

    std::string test_mic_name_;
    std::string test_name_;
    Dependencies test_deps_;
    std::unique_ptr<ResourceConfig> test_config_;
};

TEST_F(MicrophoneTest, ValidateWithValidConfig) {
  auto attributes = ProtoStruct{};

  ResourceConfig valid_config(
      "rdk:component:microphone", "", test_name_, attributes, "",
      Model("viam", "audio", "mic"), LinkConfig{}, log_level::info);

  EXPECT_NO_THROW({
    auto result = microphone::Microphone::validate(valid_config);
    EXPECT_TRUE(result.empty()); // No validation errors
  });
}

TEST_F(MicrophoneTest, ValidateWithValidOptionalAttributes) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = test_mic_name_;
  attributes["sample_rate"] = 44100;
  attributes["num_channels"] = 1;
  attributes["latency"] = 1.0;

  ResourceConfig valid_config(
      "rdk:component:microphone", "", test_name_, attributes, "",
      Model("viam", "audio", "mic"), LinkConfig{}, log_level::info);

  EXPECT_NO_THROW({
    auto result = microphone::Microphone::validate(valid_config);
    EXPECT_TRUE(result.empty()); // No validation errors
  });
}


TEST_F(MicrophoneTest, ValidateWithInvalidConfig_SampleRateNotDouble) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = test_mic_name_;
  attributes["sample_rate"] = "44100";

  ResourceConfig invalid_config(
      "rdk:component:microphone", "", test_name_, attributes, "",
      Model("viam", "audio", "mic"), LinkConfig{}, log_level::info);

  EXPECT_THROW(
      { microphone::Microphone::validate(invalid_config); },
      std::invalid_argument);
}


TEST_F(MicrophoneTest, ValidateWithInvalidConfig_DeviceNameNotString) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = 44100;

  ResourceConfig invalid_config(
      "rdk:component:microphone", "", test_name_, attributes, "",
      Model("viam", "audio", "mic"), LinkConfig{}, log_level::info);

  EXPECT_THROW(
      { microphone::Microphone::validate(invalid_config); },
      std::invalid_argument);
}

TEST_F(MicrophoneTest, ValidateWithInvalidConfig_LatencyNotDouble) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = test_mic_name_;
  attributes["latency"] = "20.0";

  ResourceConfig invalid_config(
      "rdk:component:microphone", "", test_name_, attributes, "",
      Model("viam", "audio", "mic"), LinkConfig{}, log_level::info);

  EXPECT_THROW(
      { microphone::Microphone::validate(invalid_config); },
      std::invalid_argument);
}

TEST_F(MicrophoneTest, ValidateWithInvalidConfig_LatencyNegative) {
  auto attributes = ProtoStruct{};
  attributes["device_name"] = test_mic_name_;
  attributes["latency"] = -10.0;

  ResourceConfig invalid_config(
      "rdk:component:microphone", "", test_name_, attributes, "",
      Model("viam", "audio", "mic"), LinkConfig{}, log_level::info);

  EXPECT_THROW(
      { microphone::Microphone::validate(invalid_config); },
      std::invalid_argument);
}


TEST_F(MicrophoneTest, DoCommandReturnsEmptyStruct) {
    microphone::Microphone mic(test_deps_, *test_config_, mock_pa_.get());

    ProtoStruct command{};
    auto result = mic.do_command(command);

    EXPECT_TRUE(result.empty());
}


TEST_F(MicrophoneTest, GetPropertiesReturnsCorrectValues) {
    int sample_rate = 48000;
    int num_channels = 2;

    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);

    ResourceConfig config(
        "rdk:component:audioin",
        "",
        "test_microphone",
        attributes,
        "",
        Model("viam", "audio", "microphone"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};

    // Create microphone with specific sample rate and channels
    microphone::Microphone mic(deps, config, mock_pa_.get());

    ProtoStruct extra{};
    auto props = mic.get_properties(extra);

    EXPECT_EQ(props.sample_rate_hz, sample_rate);
    EXPECT_EQ(props.num_channels, num_channels);
    ASSERT_EQ(props.supported_codecs.size(), 1);
    EXPECT_EQ(props.supported_codecs[0], viam::sdk::audio_codecs::PCM_16);

}

TEST_F(MicrophoneTest, ModelExists) {
  auto &model = microphone::Microphone::model;

  // Test that we can access the model without errors
  EXPECT_NO_THROW({
    auto model_copy = model; // Test copy constructor
  });
  EXPECT_EQ(model.to_string(), "viam:audio:microphone");
}

TEST_F(MicrophoneTest, SetsCorrectFields) {
    int sample_rate = 44100;
    int num_channels = 1;
    double test_latency_ms = 1.0;  // In milliseconds

    auto attributes = ProtoStruct{};
    attributes["device_name"] = testDeviceName;
    attributes["sample_rate"] = static_cast<double>(sample_rate);
    attributes["num_channels"] = static_cast<double>(num_channels);
    attributes["latency"] = test_latency_ms;

    ResourceConfig config(
        "rdk:component:audioin",
        "",
        "test_microphone",
        attributes,
        "",
        Model("viam", "audio", "microphone"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};

    // Setup mock expectations for device lookup
    PaStream* dummy_stream = reinterpret_cast<PaStream*>(0x1234);

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(::testing::Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(::testing::Return(&mock_device_info_));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(dummy_stream), ::testing::Return(paNoError)));
    EXPECT_CALL(*mock_pa_, startStream(::testing::_)).WillOnce(::testing::Return(paNoError));

    // Create microphone and verify it parses config correctly
    microphone::Microphone mic(deps, config, mock_pa_.get());

    // Verify behavior: member variables are set from config
    EXPECT_EQ(mic.sample_rate_, sample_rate);
    EXPECT_EQ(mic.num_channels_, num_channels);
    EXPECT_EQ(mic.device_name_, testDeviceName);
    EXPECT_DOUBLE_EQ(mic.latency_, test_latency_ms / 1000.0);  // Stored in seconds
}

TEST_F(MicrophoneTest, DefaultsToZeroLatencyWhenNotSpecified) {
    auto attributes = ProtoStruct{};
    attributes["sample_rate"] = 44100.0;
    attributes["num_channels"] = 1.0;
    // No latency specified

    ResourceConfig config(
        "rdk:component:audioin",
        "",
        "test_microphone",
        attributes,
        "",
        Model("viam", "audio", "microphone"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};
    microphone::Microphone mic(deps, config, mock_pa_.get());
    EXPECT_DOUBLE_EQ(mic.latency_, 0.01);
}

TEST_F(MicrophoneTest, UsesDeviceDefaultSampleRate) {
    // Config without sample_rate specified
    auto attributes = ProtoStruct{};
    attributes["num_channels"] = 2.0;

    ResourceConfig config(
        "rdk:component:audioin",
        "",
        "test_microphone",
        attributes,
        "",
        Model("viam", "audio", "microphone"),
        LinkConfig{},
        log_level::info
    );

    Dependencies deps{};

    // Setup mock device with specific default sample rate
    PaDeviceInfo device_info;
    device_info.name = testDeviceName;
    device_info.maxInputChannels = 2;
    device_info.defaultLowInputLatency = 0.01;
    device_info.defaultSampleRate = 48000.0;  // Device default is 48kHz

    PaStream* dummy_stream = reinterpret_cast<PaStream*>(0x1234);

    // Expectations for constructor
    EXPECT_CALL(*mock_pa_, getDefaultInputDevice())
        .WillOnce(::testing::Return(0));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0))
        .WillRepeatedly(::testing::Return(&device_info));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(
            ::testing::SetArgPointee<0>(dummy_stream),
            ::testing::Return(paNoError)
        ));
    EXPECT_CALL(*mock_pa_, startStream(::testing::_))
        .WillOnce(::testing::Return(paNoError));

    microphone::Microphone mic(deps, config, mock_pa_.get());

    EXPECT_EQ(mic.sample_rate_, 48000);
    EXPECT_EQ(mic.num_channels_, 2);
}

TEST_F(MicrophoneTest, DeviceNotFoundThrows) {
    auto config = createConfig("NonExistentDevice");

    // Mock: no devices available
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(::testing::Return(0));

    EXPECT_THROW(microphone::Microphone(test_deps_, config, mock_pa_.get()), std::runtime_error);
}

TEST_F(MicrophoneTest, OpenStreamFailureThrows) {
    auto config = createConfig();

    // Mock: device found but openStream fails
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(::testing::Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(::testing::Return(&mock_device_info_));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(paInvalidDevice));

    EXPECT_THROW(microphone::Microphone(test_deps_, config, mock_pa_.get()), std::runtime_error);
}

TEST_F(MicrophoneTest, StartStreamFailureThrows) {
    auto config = createConfig();
    PaStream* dummy_stream = reinterpret_cast<PaStream*>(0x1234);

    // Mock: openStream succeeds but startStream fails
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(::testing::Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(::testing::Return(&mock_device_info_));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(dummy_stream), ::testing::Return(paNoError)));
    EXPECT_CALL(*mock_pa_, startStream(::testing::_)).WillOnce(::testing::Return(paInternalError));
    EXPECT_CALL(*mock_pa_, closeStream(::testing::_)).WillOnce(::testing::Return(paNoError));

    EXPECT_THROW(microphone::Microphone(test_deps_, config, mock_pa_.get()), std::runtime_error);
}

TEST_F(MicrophoneTest, NumChannelsExceedsDeviceMaxThrows) {
    auto config = createConfig(testDeviceName, 44100, 8);  // Request 8 channels

    // Mock: device found but only supports 2 channels (from mock_device_info_)
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(::testing::Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(::testing::Return(&mock_device_info_));

    EXPECT_THROW(microphone::Microphone(test_deps_, config, mock_pa_.get()), std::invalid_argument);
}

TEST_F(MicrophoneTest, DefaultDeviceNotFoundThrows) {
    auto config = createConfig("");  // Empty device name = use default

    // Mock: no default device available
    EXPECT_CALL(*mock_pa_, getDefaultInputDevice()).WillOnce(::testing::Return(paNoDevice));

    EXPECT_THROW(microphone::Microphone(test_deps_, config, mock_pa_.get()), std::runtime_error);
}

TEST_F(MicrophoneTest, ReconfigureDifferentDeviceName) {
    auto config = createConfig(testDeviceName, 44100, 2);
       expectSuccessfulStreamCreation();
    microphone::Microphone mic(test_deps_, config, mock_pa_.get());
    const char* new_device_name = "New Device";
    PaStream* dummy_stream = reinterpret_cast<PaStream*>(0x1234);

    // Reconfigure with different device name
    auto new_config = createConfig(new_device_name, 44100, 2);

    PaDeviceInfo new_device;
    new_device.name = new_device_name;
    new_device.maxInputChannels = 2;
    new_device.defaultLowInputLatency = 0.01;

    EXPECT_CALL(*mock_pa_, stopStream(::testing::_)).WillOnce(::testing::Return(paNoError));
    EXPECT_CALL(*mock_pa_, closeStream(::testing::_)).WillOnce(::testing::Return(paNoError));
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(::testing::Return(2));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(1)).WillRepeatedly(::testing::Return(&new_device));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(dummy_stream), ::testing::Return(paNoError)));
    EXPECT_CALL(*mock_pa_, startStream(::testing::_)).WillOnce(::testing::Return(paNoError));

    EXPECT_NO_THROW(mic.reconfigure(test_deps_, new_config));

    EXPECT_EQ(mic.device_name_, new_device_name);
    EXPECT_EQ(mic.sample_rate_, 44100);
    EXPECT_EQ(mic.num_channels_, 2);
}


TEST_F(MicrophoneTest, ReconfigureDifferentSampleRate) {
    auto config = createConfig(testDeviceName, 44100, 2);
    expectSuccessfulStreamCreation();
    microphone::Microphone mic(test_deps_, config, mock_pa_.get());

    // reconfigure with different sample rate
    auto new_config = createConfig(testDeviceName, 2000, 2);
    PaStream* dummy_stream = reinterpret_cast<PaStream*>(0x1234);


    EXPECT_CALL(*mock_pa_, stopStream(::testing::_)).WillOnce(::testing::Return(paNoError));
    EXPECT_CALL(*mock_pa_, closeStream(::testing::_)).WillOnce(::testing::Return(paNoError));
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(::testing::Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(::testing::Return(&mock_device_info_));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(dummy_stream), ::testing::Return(paNoError)));
    EXPECT_CALL(*mock_pa_, startStream(::testing::_)).WillOnce(::testing::Return(paNoError));


    EXPECT_NO_THROW(mic.reconfigure(test_deps_, new_config));

    EXPECT_EQ(mic.device_name_, testDeviceName);
    EXPECT_EQ(mic.sample_rate_, 2000);
    EXPECT_EQ(mic.num_channels_, 2);
}


TEST_F(MicrophoneTest, ReconfigureDifferentNumChannels) {
    auto config = createConfig(testDeviceName, 44100, 2);
    expectSuccessfulStreamCreation();
    microphone::Microphone mic(test_deps_, config, mock_pa_.get());
    PaStream* dummy_stream = reinterpret_cast<PaStream*>(0x1234);

    // Reconfigure with different num channels
    auto new_config = createConfig(testDeviceName, 44100, 1);


    EXPECT_CALL(*mock_pa_, stopStream(::testing::_)).WillOnce(::testing::Return(paNoError));
    EXPECT_CALL(*mock_pa_, closeStream(::testing::_)).WillOnce(::testing::Return(paNoError));
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(::testing::Return(2));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(1)).WillRepeatedly(::testing::Return(&mock_device_info_));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::DoAll(::testing::SetArgPointee<0>(dummy_stream), ::testing::Return(paNoError)));
    EXPECT_CALL(*mock_pa_, startStream(::testing::_)).WillOnce(::testing::Return(paNoError));

    EXPECT_NO_THROW(mic.reconfigure(test_deps_, new_config));

    EXPECT_EQ(mic.device_name_, testDeviceName);
    EXPECT_EQ(mic.sample_rate_, 44100);
    EXPECT_EQ(mic.num_channels_, 1);
}

TEST_F(MicrophoneTest, MultipleConcurrentGetAudioCalls) {
    auto config = createConfig(testDeviceName, 44100, 2);
    microphone::Microphone mic(test_deps_, config, mock_pa_.get());

    // Initialize timing
    mic.audio_context_->first_sample_adc_time = 0.0;
    mic.audio_context_->stream_start_time = std::chrono::system_clock::now();
    mic.audio_context_->first_callback_captured.store(true);
    mic.audio_context_->total_samples_written.store(0);

    // Write samples in background
    std::atomic<bool> stop_writing{false};
    std::thread writer([&]() {
        for (int i = 0; i < 100000 && !stop_writing.load(); i++) {
            mic.audio_context_->write_sample(static_cast<int16_t>(i));
            if (i % 1000 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

    // Start multiple get_audio calls concurrently
    std::atomic<int> active_count{0};
    std::atomic<int> max_active{0};
    std::vector<std::thread> readers;

    for (int i = 0; i < 3; i++) {
        readers.emplace_back([&]() {
            int current = ++active_count;
            if (current > max_active.load()) {
                max_active = current;
            }

            auto handler = [](viam::sdk::AudioIn::audio_chunk&&) {
                return true;
            };
            mic.get_audio(viam::sdk::audio_codecs::PCM_16, handler, 0.2, 0, ProtoStruct{});

            --active_count;
        });
    }

    // Wait a bit then stop
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    stop_writing = true;

    writer.join();
    for (auto& t : readers) {
        t.join();
    }

    // Verify multiple readers ran concurrently
    EXPECT_GE(max_active.load(), 2);
}

TEST_F(MicrophoneTest, GetAudioReceivesChunks) {
    auto config = createConfig(testDeviceName, 44100, 1);
    microphone::Microphone mic(test_deps_, config, mock_pa_.get());

    PaStream* dummy_stream = reinterpret_cast<PaStream*>(0x1234);


    std::shared_ptr<microphone::AudioStreamContext> ctx;
    {
        std::lock_guard<std::mutex> lock(mic.stream_ctx_mu_);
        ctx = mic.audio_context_;
    }

    // Each chunk is 100ms = 4410 samples at 44.1kHz mono
    const int samples_per_chunk = 4410;
    const int num_chunks = 5;

    // Initialize timing for timestamp calculation
    ctx->first_sample_adc_time = 0.0;
    ctx->stream_start_time = std::chrono::system_clock::now();
    ctx->first_callback_captured.store(true);
    ctx->total_samples_written.store(0);

    // Write samples in background thread while get_audio runs
    std::atomic<bool> stop_writing{false};
    std::thread writer([&]() {
        for (int i = 0; i < num_chunks * samples_per_chunk && !stop_writing.load(); i++) {
        ctx->write_sample(static_cast<int16_t>(i));

    }
    });

    int chunks_received = 0;
    auto handler = [&](viam::sdk::AudioIn::audio_chunk&& chunk) {
        chunks_received++;
        EXPECT_EQ(chunk.audio_data.size(), samples_per_chunk * sizeof(int16_t));
        return chunks_received < num_chunks;
    };

    mic.get_audio(viam::sdk::audio_codecs::PCM_16, handler, 5.0, 0, ProtoStruct{});

    stop_writing = true;
    writer.join();

    EXPECT_EQ(chunks_received, num_chunks);
}

TEST_F(MicrophoneTest, GetAudioHandlerCanStopEarly) {
    auto config = createConfig(testDeviceName, 44100, 2);
    microphone::Microphone mic(test_deps_, config, mock_pa_.get());
    PaStream* dummy_stream = reinterpret_cast<PaStream*>(0x1234);


    // Push 10 chunks worth of samples but handler will stop after 3
    const int samples_per_chunk = 4410;  // 100ms at 44.1kHz mono
    const int total_chunks = 10;

    // Initialize timing for timestamp calculation
    mic.audio_context_->first_sample_adc_time = 0.0;
    mic.audio_context_->stream_start_time = std::chrono::system_clock::now();
    mic.audio_context_->first_callback_captured.store(true);
    mic.audio_context_->total_samples_written.store(0);

    // Simulate real-time audio: write samples in background thread
    std::atomic<bool> stop_writing{false};
    std::thread writer([&]() {
        for (int i = 0; i < total_chunks * samples_per_chunk && !stop_writing.load(); i++) {
            mic.audio_context_->write_sample(static_cast<int16_t>(i));
            if (i % 100 == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }
    });

    // Give writer thread time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    int chunks_received = 0;
    auto handler = [&](viam::sdk::AudioIn::audio_chunk&& chunk) {
        chunks_received++;
        return chunks_received < 3;  // Stop after 3 chunks
    };

    mic.get_audio(viam::sdk::audio_codecs::PCM_16, handler, 2.0, 0, ProtoStruct{});

    stop_writing = true;
    writer.join();

    // Should only receive 3 chunks even though 10 chunks worth were available
    EXPECT_EQ(chunks_received, 3);
}


TEST_F(MicrophoneTest, GetAudioWithInvalidCodecThrowsError) {
    microphone::Microphone mic(test_deps_, *test_config_, mock_pa_.get());

    auto handler = [](viam::sdk::AudioIn::audio_chunk&& chunk) { return true; };

    EXPECT_THROW({
        mic.get_audio("invalid_codec", handler, 0.1, 0, ProtoStruct{});
    }, std::invalid_argument);
}

TEST_F(MicrophoneTest, TestOpenStreamSuccessDefaultDevice) {
    auto config = createConfig(testDeviceName, 44100, 2);
    expectSuccessfulStreamCreation();

    microphone::Microphone mic(Dependencies{}, config, mock_pa_.get());
    PaStream* stream = nullptr;

    EXPECT_CALL(*mock_pa_, isFormatSupported(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(paNoError));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_,
                                      static_cast<double>(44100),
                                      ::testing::_, ::testing::_,
                                      ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(paNoError));
    EXPECT_NO_THROW(mic.openStream(&stream));
}


TEST_F(MicrophoneTest, TestOpenStreamSuccessSpecificDevice) {
    const char* deviceName = "test_device";
    mock_device_info_.name = deviceName;
    mock_device_info_.maxInputChannels = 2;

    auto config = createConfig(deviceName, 48000, 2);
    expectSuccessfulStreamCreation();
    microphone::Microphone mic(Dependencies{}, config, mock_pa_.get());
    PaStream* stream = nullptr;
    EXPECT_CALL(*mock_pa_, isFormatSupported(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(paNoError));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_,
                                    static_cast<double>(48000),
                                    ::testing::_, ::testing::_,
                                    ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(paNoError));
    EXPECT_NO_THROW(mic.openStream(&stream));
}


TEST_F(MicrophoneTest, TestOpenStreamFormatNotSupported) {
    auto config = createConfig(testDeviceName, 44100, 2);
    expectSuccessfulStreamCreation();

    microphone::Microphone mic(Dependencies{}, config, mock_pa_.get());

    // Test that OpenStream throws when format is not supported
    PaStream* stream = nullptr;
    EXPECT_CALL(*mock_pa_, isFormatSupported(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(paInvalidDevice));
    EXPECT_THROW(mic.openStream(&stream), std::runtime_error);
}

TEST_F(MicrophoneTest, TestOpenStreamFails) {
    auto config = createConfig(testDeviceName, 44100, 2);
    expectSuccessfulStreamCreation();

    microphone::Microphone mic(Dependencies{}, config, mock_pa_.get());
    PaStream* stream = nullptr;
    EXPECT_CALL(*mock_pa_, isFormatSupported(::testing::_, ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(paNoError));
    EXPECT_CALL(*mock_pa_, openStream(::testing::_, ::testing::_, ::testing::_,
                                    ::testing::_, ::testing::_, ::testing::_,
                                    ::testing::_, ::testing::_))
        .WillOnce(::testing::Return(paInvalidDevice));
    EXPECT_THROW(mic.openStream(&stream), std::runtime_error);
}

// AudioStreamContext validation tests
TEST_F(MicrophoneTest, AudioStreamContextThrowsOnZeroNumChannels) {
    viam::sdk::audio_info info;
    info.sample_rate_hz = 44100;
    info.num_channels = 0;  // Invalid

    EXPECT_THROW({
        microphone::AudioStreamContext ctx(info, 4410, 10);
    }, std::invalid_argument);
}

TEST_F(MicrophoneTest, AudioStreamContextThrowsOnNegativeNumChannels) {
    viam::sdk::audio_info info;
    info.sample_rate_hz = 44100;
    info.num_channels = -1;  // Invalid

    EXPECT_THROW({
        microphone::AudioStreamContext ctx(info, 4410, 10);
    }, std::invalid_argument);
}

TEST_F(MicrophoneTest, AudioStreamContextThrowsOnZeroSampleRate) {
    viam::sdk::audio_info info;
    info.sample_rate_hz = 0;  // Invalid
    info.num_channels = 2;

    EXPECT_THROW({
        microphone::AudioStreamContext ctx(info, 4410, 10);
    }, std::invalid_argument);
}

TEST_F(MicrophoneTest, AudioStreamContextThrowsOnNegativeSampleRate) {
    viam::sdk::audio_info info;
    info.sample_rate_hz = -44100;  // Invalid
    info.num_channels = 2;

    EXPECT_THROW({
        microphone::AudioStreamContext ctx(info, 4410, 10);
    }, std::invalid_argument);
}

TEST_F(MicrophoneTest, AudioStreamContextThrowsOnZeroBufferDuration) {
    viam::sdk::audio_info info;
    info.sample_rate_hz = 44100;
    info.num_channels = 2;

    EXPECT_THROW({
        microphone::AudioStreamContext ctx(info, 4410, 0);
    }, std::invalid_argument);
}

TEST_F(MicrophoneTest, AudioStreamContextThrowsOnNegativeBufferDuration) {
    viam::sdk::audio_info info;
    info.sample_rate_hz = 44100;
    info.num_channels = 2;

    EXPECT_THROW({
        microphone::AudioStreamContext ctx(info, 4410, -5);
    }, std::invalid_argument);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new MicrophoneTestEnvironment);
  return RUN_ALL_TESTS();
}
