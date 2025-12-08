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
        for (const auto& [name, input_channels, output_channels, sample_rate] : devices) {
            PaDeviceInfo info;
            info.name = name.c_str();
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
};

TEST_F(DiscoveryTest, NoDevicesFound) {
    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillOnce(Return(0));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    EXPECT_EQ(configs.size(), 0);
}

TEST_F(DiscoveryTest, SingleInputDevice) {
    createMockDevices({{"Test Microphone", 2, 0, 48000.0}});

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(1));
    EXPECT_CALL(*mock_pa_, getDeviceInfo(0)).WillRepeatedly(Return(&device_infos_[0]));

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    EXPECT_EQ(configs.size(), 1);
    EXPECT_EQ(configs[0].name(), "microphone-1");
    EXPECT_EQ(configs[0].api(), "rdk:component:audio_in");

    auto attrs = configs[0].attributes();
    EXPECT_EQ(attrs.get<std::string>("device_name").value(), "Test Microphone");
    EXPECT_EQ(attrs.get<double>("sample_rate").value(), 48000.0);
    EXPECT_EQ(attrs.get<double>("num_channels").value(), 2.0);
}

TEST_F(DiscoveryTest, MixedInputOutputDevices) {
    createMockDevices({
        {"Microphone", 2, 0, 44100.0},
        {"Speaker", 0, 2, 44100.0},
        {"Another Mic", 1, 0, 48000.0}
    });

    EXPECT_CALL(*mock_pa_, getDeviceCount()).WillRepeatedly(Return(3));
    for (int i = 0; i < 3; i++) {
        EXPECT_CALL(*mock_pa_, getDeviceInfo(i)).WillRepeatedly(Return(&device_infos_[i]));
    }

    AudioDiscovery discovery(deps_, *config_, mock_pa_.get());
    auto configs = discovery.discover_resources(ProtoStruct{});

    // Only input devices should be discovered
    EXPECT_EQ(configs.size(), 3);
    EXPECT_EQ(configs[0].name(), "microphone-1");
    EXPECT_EQ(configs[1].name(), "speaker-1");
    EXPECT_EQ(configs[2].name(), "microphone-2");
    EXPECT_EQ(configs[0].attributes().get<std::string>("device_name").value(), "Microphone");
    EXPECT_EQ(configs[2].attributes().get<std::string>("device_name").value(), "Speaker");
    EXPECT_EQ(configs[2].attributes().get<std::string>("device_name").value(), "Another Mic");

}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new test_utils::AudioTestEnvironment);
    return RUN_ALL_TESTS();
}
