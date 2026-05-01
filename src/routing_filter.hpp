#pragma once

#include "portaudio.h"

#if defined(__linux__)
#include <unordered_map>
#include <unordered_set>
#endif

namespace audio {
namespace routing {

#if defined(__linux__)
// Cross-bar routing snapshot for a single Tegra APE card.
struct CardRouting {
    std::unordered_set<int> routed_inputs;
    std::unordered_set<int> routed_outputs;
};

// Card number → routing snapshot. Only Tegra APE cards appear; cards we
// couldn't open are silently omitted (callers fall back to "not filtered").
using ApeRoutingMap = std::unordered_map<int, CardRouting>;

// Walks every ALSA card once, scanning the cross-bar muxes on each Tegra
// APE card. Call this once per discovery pass and pass the result to
// is_unrouted_admaif() for every device — the lookup itself does no I/O.
ApeRoutingMap scan_ape_cards();
#else
// Stub on non-Linux: scan_ape_cards/is_unrouted_admaif always say "not
// filtered" so downstream code doesn't need its own platform guards.
struct ApeRoutingMap {};
inline ApeRoutingMap scan_ape_cards() { return {}; }
#endif

// True iff this PortAudio device is a Tegra ADMAIF whose cross-bar has
// no path to a physical interface in the requested direction. Pure lookup
// against the pre-built `routing` map — no ALSA calls.
bool is_unrouted_admaif(const PaDeviceInfo& info, bool is_input, const ApeRoutingMap& routing);

}  // namespace routing
}  // namespace audio
