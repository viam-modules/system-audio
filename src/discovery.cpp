#include "discovery.hpp"
#include <iostream>
#include <sstream>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include <viam/sdk/services/discovery.hpp>
#include "microphone.hpp"
#include "portaudio.hpp"
#include "speaker.hpp"

namespace discovery {

namespace vsdk = ::viam::sdk;
vsdk::Model AudioDiscovery::model = vsdk::Model("viam", "system-audio", "discovery");

AudioDiscovery::AudioDiscovery(vsdk::Dependencies dependencies,
                               vsdk::ResourceConfig configuration,
                               audio::portaudio::PortAudioInterface* pa)
    : Discovery(configuration.name()), pa_(pa) {}

std::vector<vsdk::ResourceConfig> AudioDiscovery::discover_resources(const vsdk::ProtoStruct& extra) {
    std::vector<vsdk::ResourceConfig> configs;

    const int numDevices = pa_ ? pa_->getDeviceCount() : Pa_GetDeviceCount();

    if (numDevices <= 0) {
        VIAM_RESOURCE_LOG(warn) << "No audio devices found during discovery";
        return {};
    }

    VIAM_RESOURCE_LOG(info) << "Discovery found " << numDevices << " audio devices";

    // Helper lambda to create device configs
    auto create_device_config = [this](const std::string& component_type,
                                       const std::string& device_type,
                                       const std::string& api,
                                       const std::string& device_name,
                                       const double sample_rate,
                                       const int num_channels,
                                       int count,
                                       const vsdk::Model& model) -> vsdk::ResourceConfig {
        try {
            vsdk::ProtoStruct attributes;
            attributes.emplace("device_name", device_name);
            attributes.emplace("sample_rate", sample_rate);
            attributes.emplace("num_channels", num_channels);

            std::stringstream name;
            name << device_type << "-" << count;

            return vsdk::ResourceConfig(component_type,
                                        name.str(),
                                        "viam",  // namespace
                                        attributes,
                                        api,
                                        model,
                                        vsdk::log_level::info);
        } catch (std::exception& e) {
            std::stringstream buffer;
            buffer << "Failed to create resource config for " << device_type << " device: " << device_name << " : " << e.what();
            VIAM_RESOURCE_LOG(error) << buffer.str();
            throw std::runtime_error(buffer.str());
        }
    };

    auto add_device_config = [&](const vsdk::Model& model,
                                 const std::string& component_type,
                                 const std::string& device_type,
                                 const std::string& api,
                                 const std::string& device_name,
                                 const double sample_rate,
                                 const int num_channels,
                                 int& counter) {
        ++counter;

        std::stringstream deviceInfoString;
        deviceInfoString << "discovered " << device_name << ", default sample rate: " << sample_rate << ", max channels: " << num_channels;
        VIAM_RESOURCE_LOG(debug) << deviceInfoString.str();

        vsdk::ResourceConfig config =
            create_device_config(component_type, device_type, api, device_name, sample_rate, num_channels, counter, model);
        configs.push_back(config);
    };
    int count_input = 0;
    int count_output = 0;

    for (int i = 0; i < numDevices; i++) {
        const PaDeviceInfo* info = pa_ ? pa_->getDeviceInfo(i) : Pa_GetDeviceInfo(i);
        const std::string device_name = info->name;
        const double sample_rate = info->defaultSampleRate;

        if (info->maxInputChannels > 0) {
            add_device_config(microphone::Microphone::model,
                              "audio_in",
                              "microphone",
                              "rdk:component:audio_in",
                              device_name,
                              sample_rate,
                              info->maxInputChannels,
                              count_input);
        }
        if (info->maxOutputChannels > 0) {
            add_device_config(speaker::Speaker::model,
                              "audio_out",
                              "speaker",
                              "rdk:component:audio_out",
                              device_name,
                              sample_rate,
                              info->maxOutputChannels,
                              count_output);
        }
    }

    return configs;
}

vsdk::ProtoStruct AudioDiscovery::do_command(const vsdk::ProtoStruct& command) {
    VIAM_RESOURCE_LOG(error) << "do_command not implemented";
    return vsdk::ProtoStruct{};
}

}  // namespace discovery
