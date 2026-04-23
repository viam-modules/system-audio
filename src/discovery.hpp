#pragma once
#include <memory>

#include <viam/sdk/config/resource.hpp>
#include <viam/sdk/resource/reconfigurable.hpp>
#include <viam/sdk/services/discovery.hpp>
#include "device_id.hpp"
#include "portaudio.hpp"

namespace discovery {

class AudioDiscovery : public viam::sdk::Discovery {
   public:
    explicit AudioDiscovery(viam::sdk::Dependencies dependencies,
                            viam::sdk::ResourceConfig configuration,
                            audio::portaudio::PortAudioInterface* pa = nullptr,
                            audio::device_id::DeviceIdResolver* resolver = nullptr);
    std::vector<viam::sdk::ResourceConfig> discover_resources(const viam::sdk::ProtoStruct& extra) override;
    viam::sdk::ProtoStruct do_command(const viam::sdk::ProtoStruct& command) override;
    static viam::sdk::Model model;

   private:
    // These are null in production and used for testing to inject mocks.
    const audio::portaudio::PortAudioInterface* pa_{};
    audio::device_id::RealDeviceIdResolver default_resolver_{};
    const audio::device_id::DeviceIdResolver* resolver_{};
};
}  // namespace discovery
