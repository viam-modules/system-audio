#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "portaudio.h"

namespace audio {
namespace routing {

// Cross-bar routing snapshot for a single Tegra APE card.
struct CardRouting {
    std::unordered_set<int> routed_inputs;
    std::unordered_set<int> routed_outputs;
};

// Card number → routing snapshot. Only Tegra APE cards appear; cards we
// couldn't open (or that don't exist on this platform) are absent.
using ApeRoutingMap = std::unordered_map<int, CardRouting>;

// Walks every ALSA card once, scanning the cross-bar muxes on each Tegra
// APE card. Call this once per discovery pass and pass the result to
// is_unrouted_admaif() for every device — the lookup itself does no I/O.
// Returns an empty map on non-Linux platforms.
ApeRoutingMap scan_ape_cards();

// True iff this PortAudio device is a Tegra ADMAIF whose cross-bar has
// no path to a physical interface in the requested direction. Pure lookup
// against the pre-built `routing` map — no syscalls.
bool is_unrouted_admaif(const PaDeviceInfo& info, bool is_input, const ApeRoutingMap& routing);

// Parses "ADMAIF12" → 12. Returns -1 on mismatch.
int parse_admaif_index(const std::string& s, std::size_t pos = 0);

}  // namespace routing
}  // namespace audio
