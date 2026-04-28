#include "routing_filter.hpp"

#if defined(__linux__)

#include <alsa/asoundlib.h>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_set>

#include <viam/sdk/common/utils.hpp>

#include "file_utils.hpp"

namespace audio {
namespace routing {

namespace {

// APE is the Tegra SoC's on-chip audio router. Its PCMs are DMA channels,
// not real I/O.
bool is_tegra_ape_card(const std::string& card_num) {
    const std::string id = audio::utils::read_file("/sys/class/sound/card" + card_num + "/id");
    return id == "APE";
}

bool ends_with(const std::string& s, const char* suffix) {
    const size_t n = std::char_traits<char>::length(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// Parses "ADMAIF12" → 12. Returns -1 on mismatch.
int parse_admaif_index(const std::string& s, size_t pos = 0) {
    constexpr std::string_view ADMAIF_PREFIX = "ADMAIF";
    if (s.compare(pos, ADMAIF_PREFIX.size(), ADMAIF_PREFIX) != 0) {
        return -1;
    }
    const size_t digit_start = pos + ADMAIF_PREFIX.size();
    if (digit_start >= s.size() || !std::isdigit(static_cast<unsigned char>(s[digit_start]))) {
        return -1;
    }
    return std::stoi(s.substr(digit_start));
}

unsigned int read_enum_value(snd_ctl_t* ctl, snd_ctl_elem_id_t* id) {
    snd_ctl_elem_value_t* value;
    snd_ctl_elem_value_alloca(&value);
    snd_ctl_elem_value_set_id(value, id);
    if (snd_ctl_elem_read(ctl, value) < 0) {
        return 0;
    }
    return snd_ctl_elem_value_get_enumerated(value, 0);
}

std::string read_item_name(snd_ctl_t* ctl, snd_ctl_elem_id_t* id, unsigned int idx) {
    snd_ctl_elem_info_t* info;
    snd_ctl_elem_info_alloca(&info);
    snd_ctl_elem_info_set_id(info, id);
    if (snd_ctl_elem_info(ctl, info) < 0) {
        return "";
    }
    if (snd_ctl_elem_info_get_type(info) != SND_CTL_ELEM_TYPE_ENUMERATED) {
        return "";
    }
    snd_ctl_elem_info_set_item(info, idx);
    if (snd_ctl_elem_info(ctl, info) < 0) {
        return "";
    }
    const char* item = snd_ctl_elem_info_get_item_name(info);
    return item ? std::string(item) : std::string();
}

// Walks every enum control on `ctl` and records:
//   - which ADMAIF<n> capture muxes are set non-None  → routed_inputs
//   - which non-ADMAIF muxes currently select an "ADMAIF<n>" source → routed_outputs
void scan_card(snd_ctl_t* ctl, std::unordered_set<int>& routed_inputs, std::unordered_set<int>& routed_outputs) {
    snd_ctl_elem_list_t* list;
    snd_ctl_elem_list_alloca(&list);

    if (snd_ctl_elem_list(ctl, list) < 0) {
        return;
    }
    const unsigned int count = snd_ctl_elem_list_get_count(list);
    if (count == 0) {
        return;
    }
    if (snd_ctl_elem_list_alloc_space(list, count) < 0) {
        return;
    }
    if (snd_ctl_elem_list(ctl, list) < 0) {
        snd_ctl_elem_list_free_space(list);
        return;
    }

    snd_ctl_elem_id_t* id;
    snd_ctl_elem_id_alloca(&id);

    for (unsigned int i = 0; i < count; ++i) {
        snd_ctl_elem_list_get_id(list, i, id);
        const char* raw_name = snd_ctl_elem_id_get_name(id);
        if (!raw_name) {
            continue;
        }
        const std::string name = raw_name;
        const bool is_admaif_capture_mux = name.rfind("ADMAIF", 0) == 0 && ends_with(name, " Mux");
        if (is_admaif_capture_mux) {
            const int n = parse_admaif_index(name);
            if (n < 0) {
                continue;
            }
            if (read_enum_value(ctl, id) != 0) {
                routed_inputs.insert(n);
            }
            continue;
        }
        if (!ends_with(name, " Mux") && !ends_with(name, " Source")) {
            continue;
        }
        const unsigned int idx = read_enum_value(ctl, id);
        if (idx == 0) {
            continue;
        }
        const std::string item = read_item_name(ctl, id, idx);
        const int n = parse_admaif_index(item);
        if (n >= 0) {
            routed_outputs.insert(n);
        }
    }

    snd_ctl_elem_list_free_space(list);
}

}  // namespace

bool is_unrouted_admaif(const PaDeviceInfo& info, bool is_input) {
    if (!info.name) {
        return false;
    }
    const auto hw = audio::utils::parse_alsa_hw(info.name);
    if (!hw) {
        return false;
    }
    const std::string card_str = std::to_string(hw->card_num);
    if (!is_tegra_ape_card(card_str)) {
        return false;
    }

    snd_ctl_t* raw_ctl = nullptr;
    const std::string ctl_name = "hw:" + card_str;
    if (snd_ctl_open(&raw_ctl, ctl_name.c_str(), 0) < 0) {
        VIAM_SDK_LOG(warn) << "[routing_filter] snd_ctl_open failed for " << ctl_name
                           << "; not filtering its PCMs";
        return false;
    }
    const std::unique_ptr<snd_ctl_t, int (*)(snd_ctl_t*)> ctl(raw_ctl, &snd_ctl_close);

    std::unordered_set<int> routed_inputs;
    std::unordered_set<int> routed_outputs;
    scan_card(ctl.get(), routed_inputs, routed_outputs);

    // On Tegra APE, ALSA PCM device N exposes ADMAIF<N+1>. Verified against
    // `aplay -l` on Orin Nano: device 0 → XBAR-ADMAIF1-0, device 1 →
    // XBAR-ADMAIF2-1, etc.
    const int admaif_n = hw->device_num + 1;
    const auto& set = is_input ? routed_inputs : routed_outputs;
    return set.count(admaif_n) == 0;
}

}  // namespace routing
}  // namespace audio

#else

namespace audio {
namespace routing {

bool is_unrouted_admaif(const PaDeviceInfo&, bool) {
    return false;
}

}  // namespace routing
}  // namespace audio

#endif
