#include "discovery.hpp"
#include "portaudio.hpp"
#include "microphone.hpp"
#include <iostream>
#include <sstream>
#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/services/discovery.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>

namespace discovery {

namespace vsdk = ::viam::sdk;
vsdk::Model AudioDiscovery::model = vsdk::Model("viam", "audio", "discovery");

AudioDiscovery::AudioDiscovery(vsdk::Dependencies dependencies, vsdk::ResourceConfig configuration)
    : Discovery(configuration.name()) {}



std::vector<vsdk::ResourceConfig> AudioDiscovery::discover_resources(const vsdk::ProtoStruct& extra) {
    std::vector<vsdk::ResourceConfig> configs;


    int numDevices = Pa_GetDeviceCount();


    if (numDevices <= 0) {
        VIAM_RESOURCE_LOG(warn) << "No audio devices found during discovery";
        return {};
    }

    VIAM_SDK_LOG(info) << "Discovery found " << numDevices << " audio devices";

    bool input;
    bool output;
    int count_input = 0;
    int count_output = 0;
    for (int i = 0; i < numDevices; i++) {
          input = false;
          output = false;
          const PaDeviceInfo* info = Pa_GetDeviceInfo(i);
          std::string device_name = info->name;
          double sample_rate = info->defaultSampleRate;
          int num_input_channels = 0;
          int num_output_channels = 0;

          if (info->maxInputChannels > 0) {
                input = true;
                num_input_channels = info->maxInputChannels;
          } else if(info->maxOutputChannels > 0) {
                output = true;
                num_output_channels = info->maxOutputChannels;
          }

            if (input) {
                std::stringstream deviceInfoString;
                deviceInfoString << "Microphone " << (i + 1) << " - Name: " << device_name << ", default sample rate:  " << sample_rate
                                    << ", max channels: " << num_input_channels;
                VIAM_SDK_LOG(info) << deviceInfoString.str();
                vsdk::ProtoStruct attributes;
                attributes.emplace("device_name", device_name);
                attributes.emplace("sample_rate", sample_rate);
                attributes.emplace("num_channels", num_input_channels);

                std::stringstream name;
                ++count_input;
                name << "microphone-" << count_input;

                try {
                    vsdk::ResourceConfig config(
                    "audio_in", std::move(name.str()), "viam", attributes, "rdk:component:audio_in", microphone::Microphone::model, vsdk::log_level::info);
                    configs.push_back(config);
                 } catch(std::exception& e) {
                    std::stringstream buffer;
                    buffer << "Failed to create resource config for input device: " << device_name << " : " << e.what();
                    VIAM_SDK_LOG(error) << buffer.str();
                    throw std::runtime_error(buffer.str());

                 }
            }
            if (output) {
                //TODO: rm when speaker is in
                continue;
                std::stringstream deviceInfoString;
                deviceInfoString << "Speaker " << (i + 1) << " - Name: " << device_name << ", default sample rate:  " << sample_rate
                                    << ", max channels: " << num_output_channels;
                VIAM_SDK_LOG(info) << deviceInfoString.str();

                vsdk::ProtoStruct attributes;
                attributes.emplace("device_name", device_name);
                attributes.emplace("sample_rate", sample_rate);
                attributes.emplace("num_channels", num_output_channels);


                std::stringstream name;
                ++count_output;
                name << "speaker-" << count_output;
                try {
                    vsdk::ResourceConfig config(
                    "audio-out", std::move(name.str()), "viam", attributes, "rdk:component:audio_out", microphone::Microphone::model, vsdk::log_level::info);
                    configs.push_back(config);
                } catch(std::exception& e) {
                    std::stringstream buffer;
                    buffer << "Failed to create resource config for output device " << device_name << " : " << e.what();
                    VIAM_SDK_LOG(error) << buffer.str();
                    throw std::runtime_error(buffer.str());
                 }
            }
    }

    return configs;
}

vsdk::ProtoStruct AudioDiscovery::do_command(const vsdk::ProtoStruct& command) {
    VIAM_RESOURCE_LOG(error) << "do_command not implemented";
    return vsdk::ProtoStruct{};
}


} // namespace discovery
