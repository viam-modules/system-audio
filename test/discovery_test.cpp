#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <viam/sdk/common/instance.hpp>
#include <viam/sdk/config/resource.hpp>
#include <portaudio.h>
#include "discovery.hpp"
#include "microphone.hpp"
#include "test_utils.hpp"

using namespace viam::sdk;
using namespace discovery;
using ::testing::Return;
using ::testing::_;

class DiscoveryTest : public test_utils::AudioTestBase {
protected:
    void SetUp() override {
        AudioTestBase::SetUp();

        mock_resolver_ = std::make_unique<::testing::NiceMock<test_utils::MockDeviceIdResolver>>();
        ON_CALL(*mock_resolver_, resolve(_, _)).WillByDefault(Return(std::string{}));

        auto attributes = ProtoStruct{};
        config_ = std::make_unique<ResourceConfig>(
            "rdk:service:discovery", "", "test_discovery", attributes, "",
            AudioDiscovery::model, LinkConfig{}, log_level::info
        );
        deps_ = Dependencies{};
    }

    // Helper to create and store mock device infos
    void createMockDevices(const std::vector<std::tuple<std::string, int, int, double>>& devices) {
        device_infos_.clear();
        device_names_.clear();
        device_names_.reserve(devices.size());
        for (const auto& [name, input_channels, output_channels, sample_rate] : devices) {
            device_names_.push_back(name); // persist string
            PaDeviceInfo info;
            info.name = device_names_.back().c_str();
            info.maxInputChannels = input_channels;
            info.maxOutputChannels = output_channels;
            info.defaultSampleRate = sample_rate;
            info.defaultLowInputLatency = 0.01;
            info.defaultLowOutputLatency = 0.01;
            info.defaultHighInputLatency = 0.1;
            info.defaultHighOutputLatency = 0.1;
            device_infos_.push_back(info);
        }
    }

    std::unique_ptr<ResourceConfig> config_;
    Dependencies deps_;
    std::vector<PaDeviceInfo> device_infos_;
    std::vector<std::string> device_names_;
    std::unique_ptr<::testing::NiceMock<test_utils::MockDeviceIdResolver>> mock_resolver_;
};

TEST_F(DiscoveryTest, NoDevicesFound) {
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(Return(0));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get(), mock_resolver_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    EXPECT_EQ(configs.size(), 0);
}

TEST_F(DiscoveryTest, SingleInputDevice) {
    std::string test_name =" Test Microphone";
    createMockDevices({{test_name, 2, 0, 48000.0}});

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&device_infos_[0]));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get(), mock_resolver_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    EXPECT_EQ(configs.size(), 1);
    EXPECT_EQ(std::string(configs[0].name()), "microphone-1");
    EXPECT_EQ(configs[0].api().to_string(), "rdk:component:audio_in");

    auto attrs = configs[0].attributes();
    auto it_name = attrs.find("device_name");
    const std::string* str_ptr = it_name->second.get<std::string>();
    ASSERT_NE(str_ptr, nullptr);
    EXPECT_EQ(*str_ptr, test_name);

    // sample_rate
    auto it_rate = attrs.find("sample_rate");
    const double* rate_ptr = it_rate->second.get<double>();
    ASSERT_NE(rate_ptr, nullptr);
    EXPECT_EQ(*rate_ptr, 48000.0);

    // num_channels
    auto it_channels = attrs.find("num_channels");
    const double* channels_ptr = it_channels->second.get<double>();
    ASSERT_NE(channels_ptr, nullptr);
    EXPECT_EQ(*channels_ptr, 2);

}


TEST_F(DiscoveryTest, SingleOutputDevice) {
    std::string test_name ="Test Speaker";
    createMockDevices({{test_name, 0, 2, 48000.0}});

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&device_infos_[0]));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get(), mock_resolver_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    EXPECT_EQ(configs.size(), 1);
    EXPECT_EQ(std::string(configs[0].name()), "speaker-1");
    EXPECT_EQ(configs[0].api().to_string(), "rdk:component:audio_out");

    auto attrs = configs[0].attributes();

    auto dev_name = attrs.find("device_name");
    const std::string* str_ptr = dev_name->second.get<std::string>();
    ASSERT_NE(str_ptr, nullptr);
    EXPECT_EQ(*str_ptr, test_name);

    // sample_rate
    auto it_rate = attrs.find("sample_rate");
    const double* rate_ptr = it_rate->second.get<double>();
    ASSERT_NE(rate_ptr, nullptr);
    EXPECT_EQ(*rate_ptr, 48000.0);

    // num_channels
    auto it_channels = attrs.find("num_channels");
    const double* channels_ptr = it_channels->second.get<double>();
    ASSERT_NE(channels_ptr, nullptr);
    EXPECT_EQ(*channels_ptr, 2);
}


TEST_F(DiscoveryTest, DeviceIdAttributePassThrough) {
    // A discovered device should surface the resolver's stable id verbatim
    // as the "device_id" attribute. The resolver is invoked with the
    // PortAudio index of the underlying device.
    const std::string test_name = "Test Microphone";
    const std::string stable_id = "usb:046d:0825:ABC123";
    createMockDevices({{test_name, 2, 0, 48000.0}});

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&device_infos_[0]));
    EXPECT_CALL(*mock_resolver_, resolve(0, _)).WillOnce(Return(stable_id));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get(), mock_resolver_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    ASSERT_EQ(configs.size(), 1);
    auto attrs = configs[0].attributes();
    auto it_id = attrs.find("device_id");
    ASSERT_NE(it_id, attrs.end());
    const std::string* id_ptr = it_id->second.get<std::string>();
    ASSERT_NE(id_ptr, nullptr);
    EXPECT_EQ(*id_ptr, stable_id);
}

TEST_F(DiscoveryTest, DeviceIdEmptyWhenResolverReturnsEmpty) {
    // Resolver may return "" for virtual / unsupported devices; the attribute
    // must still be present so downstream consumers can rely on it.
    const std::string test_name = "pulse";
    createMockDevices({{test_name, 2, 0, 44100.0}});

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&device_infos_[0]));
    EXPECT_CALL(*mock_resolver_, resolve(0, _)).WillOnce(Return(std::string{}));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get(), mock_resolver_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    ASSERT_EQ(configs.size(), 1);
    auto attrs = configs[0].attributes();
    auto it_id = attrs.find("device_id");
    ASSERT_NE(it_id, attrs.end());
    const std::string* id_ptr = it_id->second.get<std::string>();
    ASSERT_NE(id_ptr, nullptr);
    EXPECT_EQ(*id_ptr, "");
}

TEST_F(DiscoveryTest, DeviceIdSharedBetweenInputAndOutputHalves) {
    // A device with both input and output channels yields two resource
    // configs — they should share the same resolved device_id since they
    // refer to the same physical hardware.
    const std::string test_name = "Combo Device";
    const std::string stable_id = "AppleHDAEngineOutput:1B,0,1,1:0";
    createMockDevices({{test_name, 2, 2, 48000.0}});

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&device_infos_[0]));
    EXPECT_CALL(*mock_resolver_, resolve(0, _)).WillRepeatedly(Return(stable_id));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get(), mock_resolver_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    ASSERT_EQ(configs.size(), 2);
    for (const auto& cfg : configs) {
        auto attrs = cfg.attributes();
        auto it_id = attrs.find("device_id");
        ASSERT_NE(it_id, attrs.end());
        const std::string* id_ptr = it_id->second.get<std::string>();
        ASSERT_NE(id_ptr, nullptr);
        EXPECT_EQ(*id_ptr, stable_id);
    }
}

TEST_F(DiscoveryTest, MixedInputOutputDevices) {
    std::string test_mic_name ="mic";
    std::string test_mic_name2 ="mic2";
    std::string test_speaker = "speaker";
    createMockDevices({
        {test_mic_name, 2, 0, 44100.0},
        {test_speaker, 0, 2, 44100.0},
        {test_mic_name2, 1, 0, 48000.0}
    });

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(3));
    for (int i = 0; i < 3; i++) {
        EXPECT_CALL(*mock_pa_, getDeviceInfo(i)).WillRepeatedly(Return(&device_infos_[i]));
    }

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get(), mock_resolver_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    EXPECT_EQ(configs.size(), 3);
    EXPECT_EQ(configs[0].name(), "microphone-1");
    EXPECT_EQ(configs[1].name(), "speaker-1");
    EXPECT_EQ(configs[2].name(), "microphone-2");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
