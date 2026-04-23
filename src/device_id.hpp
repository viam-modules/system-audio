#pragma once

#include <string>

#include "portaudio.h"

namespace audio {
namespace device_id {

// Resolves a stable identifier for a PortAudio device that persists across
// reboots. An empty string means no stable identifier is available — e.g.
// for virtual ALSA endpoints like "default", or when sysfs/Core Audio lookup
// did not find a match.
class DeviceIdResolver {
   public:
    virtual std::string resolve(PaDeviceIndex index, const PaDeviceInfo& info) const = 0;
    virtual ~DeviceIdResolver() = default;
};

// Production resolver. On macOS it returns Core Audio's
// kAudioDevicePropertyDeviceUID via name matching. On Linux it returns
// "usb:VID:PID[:SERIAL]" for USB devices or "alsa-card:<kernel-id>" for
// built-in hardware, via sysfs. On other platforms it always returns "".
class RealDeviceIdResolver : public DeviceIdResolver {
   public:
    std::string resolve(PaDeviceIndex index, const PaDeviceInfo& info) const override;
};

}  // namespace device_id
}  // namespace audio
