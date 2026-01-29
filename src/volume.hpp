#pragma once

#include <algorithm>
#include <string>
#include <viam/sdk/common/utils.hpp>
#include "audio_utils.hpp"

namespace audio {
namespace volume {

#ifdef __linux__
#include <alsa/asoundlib.h>

// Extract ALSA card identifier from PortAudio device name.
// PortAudio names look like "bcm2835 Headphones: - (hw:0,0)" on Pi.
inline std::string extract_alsa_card(const std::string& device_name) {
    const std::vector<std::string> prefixes = {"plughw:", "hw:"};

    for (const auto& prefix : prefixes) {
        const auto pos = device_name.find(prefix);
        if (pos != std::string::npos) {
            const auto comma = device_name.find(',', pos);
            const auto paren = device_name.find(')', pos);
            const auto end = std::min(comma, paren);
            if (end != std::string::npos) {
                return device_name.substr(pos, end - pos);
            }
        }
    }
    VIAM_SDK_LOG(warn) << "[set_volume] Couldn't find device card, falling back to default";
    return "default";
}

inline void set_volume(const std::string& device_name, int volume) {
    const std::string card = extract_alsa_card(device_name);

    VIAM_SDK_LOG(debug) << "[set_volume] Setting ALSA volume to " << volume << " on card " << card;

    snd_mixer_t* mixer_ptr = nullptr;
    if (int err = snd_mixer_open(&mixer_ptr, 0); err < 0) {
        VIAM_SDK_LOG(error) << "[set_volume] Failed to open ALSA mixer: " << snd_strerror(err);
        return;
    }
    audio::utils::CleanupPtr<snd_mixer_close> mixer(mixer_ptr);

    // Connect alsa mixer to our device's sound card
    if (int err = snd_mixer_attach(mixer.get(), card.c_str()); err < 0) {
        VIAM_SDK_LOG(error) << "[set_volume] Failed to attach mixer to card: " << card << " :  " << snd_strerror(err);
        return;
    }

    // Register simple element class to access high-level volume control
    if (int err = snd_mixer_selem_register(mixer.get(), nullptr, nullptr); err < 0) {
        VIAM_SDK_LOG(error) << "[set_volume] Failed to register mixer elements: " << snd_strerror(err);
        return;
    }

    // load elements (controls of the mixer)
    if (int err = snd_mixer_load(mixer.get()); err < 0) {
        VIAM_SDK_LOG(error) << "[set_volume] Failed to load mixer elements: " << snd_strerror(err);
        return;
    }

    // Try "PCM" first (most common for Pi), then "Master"
    snd_mixer_selem_id_t* sid = nullptr;
    snd_mixer_selem_id_alloca(&sid);

    // Volume control elements will either be called PCM, Master or Speaker depending on the device
    const char* element_names[] = {"PCM", "Master", "Speaker"};
    snd_mixer_elem_t* elem = nullptr;

    for (const char* name : element_names) {
        snd_mixer_selem_id_set_index(sid, 0);
        snd_mixer_selem_id_set_name(sid, name);
        elem = snd_mixer_find_selem(mixer.get(), sid);
        if (elem) {
            VIAM_SDK_LOG(debug) << "[set_volume] Found mixer element: " << name;
            break;
        }
    }

    if (!elem) {
        VIAM_SDK_LOG(error) << "[set_volume] Could not find PCM or Master mixer element";
        return;
    }

    long min = 0;
    long max = 0;
    snd_mixer_selem_get_playback_volume_range(elem, &min, &max);

    const long target = min + (max - min) * volume / 100;
    if (int err = snd_mixer_selem_set_playback_volume_all(elem, target); err < 0) {
        VIAM_SDK_LOG(error) << "[set_volume] Failed to set playback volume: " << snd_strerror(err);
    }
}

#else
inline void set_volume(const std::string& /*device_name*/, int /*volume*/) {
    VIAM_SDK_LOG(warn) << "[set_volume] Volume attribute is not supported on this platform";
}

#endif

}  // namespace volume
}  // namespace audio
