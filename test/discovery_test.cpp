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
};

TEST_F(DiscoveryTest, NoDevicesFound) {
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(Return(0));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    EXPECT_EQ(configs.size(), 0);
}

TEST_F(DiscoveryTest, SingleInputDevice) {
    std::string test_name =" Test Microphone";
    createMockDevices({{test_name, 2, 0, 48000.0}});

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&device_infos_[0]));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get());
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

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get());
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

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get());
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
