#pragma once
#include <memory>

#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include <viam/sdk/services/discovery.hpp>
#include "portaudio.hpp"

namespace discovery {

class AudioDiscovery : public viam::sdk::Discovery {
   public:
    explicit AudioDiscovery(viam::sdk::Dependencies dependencies, viam::sdk::ResourceConfig configuration, audio::portaudio::PortAudioInterface* pa = nullptr);
    std::vector<viam::sdk::ResourceConfig> discover_resources(const viam::sdk::ProtoStruct& extra) override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    static viam::sdk::Model model;

   private:
    // This is null in production and used for testing to inject the mock portaudio functions
    const audio::portaudio::PortAudioInterface* pa_;
};
}  // namespace discovery
