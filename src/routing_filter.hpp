#pragma once

#include "portaudio.h"

namespace audio {
namespace routing {

// True iff this PortAudio device is a Tegra ADMAIF whose cross-bar has
// no path to a physical interface in the requested direction. Returns
// false on non-Linux platforms and for any device that is not an
// ALSA hw:X,Y endpoint on a Tegra APE card.
bool is_unrouted_admaif(const PaDeviceInfo& info, bool is_input);

}  // namespace routing
}  // namespace audio
