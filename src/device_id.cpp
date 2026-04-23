#include "device_id.hpp"

#include <string>

#include <viam/sdk/common/utils.hpp>

#if defined(__APPLE__)

#include <CoreAudio/CoreAudio.h>
#include <CoreFoundation/CoreFoundation.h>
#include <vector>

namespace audio {
namespace device_id {

namespace {

// kAudioObjectPropertyElementMain was introduced in macOS 12; earlier SDKs
// spell it kAudioObjectPropertyElementMaster. Both are value 0 — define our
// own constant so we compile cleanly against either.
constexpr AudioObjectPropertyElement kMainElement = 0;

// Reads a CFString property from a Core Audio object into a std::string.
// Returns empty on failure.
std::string get_string_property(AudioObjectID object, AudioObjectPropertySelector selector) {
    AudioObjectPropertyAddress addr{selector, kAudioObjectPropertyScopeGlobal, kMainElement};
    CFStringRef value = nullptr;
    UInt32 size = sizeof(value);
    OSStatus err = AudioObjectGetPropertyData(object, &addr, 0, nullptr, &size, &value);
    if (err != noErr || !value) {
        return "";
    }

    // Size for UTF-8 encoding — device names can include multi-byte chars.
    const CFIndex length = CFStringGetLength(value);
    const CFIndex max_bytes = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    std::string out(static_cast<size_t>(max_bytes), '\0');
    const Boolean ok = CFStringGetCString(value, out.data(), max_bytes, kCFStringEncodingUTF8);
    CFRelease(value);
    if (!ok) {
        return "";
    }
    out.resize(std::char_traits<char>::length(out.c_str()));
    return out;
}

// Returns true if the Core Audio device exposes at least one stream in the
// requested scope. Lets us disambiguate when one device name belongs to both
// an input half and an output half of the same Core Audio device.
bool has_streams(AudioDeviceID device, AudioObjectPropertyScope scope) {
    AudioObjectPropertyAddress addr{kAudioDevicePropertyStreams, scope, kMainElement};
    UInt32 size = 0;
    const OSStatus err = AudioObjectGetPropertyDataSize(device, &addr, 0, nullptr, &size);
    if (err != noErr) {
        return false;
    }
    return size > 0;
}

}  // namespace

std::string RealDeviceIdResolver::resolve(PaDeviceIndex /*index*/, const PaDeviceInfo& info) const {
    if (!info.name) {
        return "";
    }
    const std::string target_name = info.name;

    // PortAudio does not publicly expose the underlying Core Audio
    // AudioDeviceID for a PaDeviceIndex, so we enumerate Core Audio devices
    // and match by name.
    AudioObjectPropertyAddress devices_addr{kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal, kMainElement};
    UInt32 data_size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &devices_addr, 0, nullptr, &data_size) != noErr) {
        VIAM_SDK_LOG(warn) << "[device_id] Failed to query Core Audio device list size for \"" << target_name << "\"";
        return "";
    }
    const UInt32 device_count = data_size / sizeof(AudioDeviceID);
    if (device_count == 0) {
        return "";
    }
    std::vector<AudioDeviceID> devices(device_count);
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &devices_addr, 0, nullptr, &data_size, devices.data()) != noErr) {
        VIAM_SDK_LOG(warn) << "[device_id] Failed to enumerate Core Audio devices for \"" << target_name << "\"";
        return "";
    }

    const bool want_input = info.maxInputChannels > 0;
    const bool want_output = info.maxOutputChannels > 0;

    AudioDeviceID fallback = kAudioObjectUnknown;
    for (const AudioDeviceID device : devices) {
        if (get_string_property(device, kAudioObjectPropertyName) != target_name) {
            continue;
        }
        if (fallback == kAudioObjectUnknown) {
            fallback = device;
        }
        const bool has_in = has_streams(device, kAudioDevicePropertyScopeInput);
        const bool has_out = has_streams(device, kAudioDevicePropertyScopeOutput);
        if ((want_input && has_in) || (want_output && has_out)) {
            return get_string_property(device, kAudioDevicePropertyDeviceUID);
        }
    }
    if (fallback != kAudioObjectUnknown) {
        return get_string_property(fallback, kAudioDevicePropertyDeviceUID);
    }
    VIAM_SDK_LOG(debug) << "[device_id] No Core Audio match for PortAudio device \"" << target_name << "\"";
    return "";
}

}  // namespace device_id
}  // namespace audio

#elif defined(__linux__)

#include <dirent.h>
#include <unistd.h>
#include <cctype>
#include <climits>
#include <fstream>
#include <regex>
#include <sstream>

namespace audio {
namespace device_id {

namespace {

std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return trim(ss.str());
}

std::string basename_of(const std::string& path) {
    const auto pos = path.find_last_of('/');
    return pos == std::string::npos ? path : path.substr(pos + 1);
}

// True if a sound device node basename (e.g. "controlC1", "pcmC1D0p")
// belongs to the given ALSA card number.
bool node_belongs_to_card(const std::string& node, const std::string& card_num) {
    size_t i = 0;
    if (node.compare(0, 8, "controlC") == 0) {
        i = 8;
    } else if (node.compare(0, 4, "pcmC") == 0) {
        i = 4;
    } else {
        return false;
    }
    return node.compare(i, card_num.size(), card_num) == 0 &&
           (i + card_num.size() == node.size() || !std::isdigit(static_cast<unsigned char>(node[i + card_num.size()])));
}

// Scans a udev-maintained directory (/dev/snd/by-id or /dev/snd/by-path)
// for a symlink pointing at a sound node belonging to the given card, and
// returns the symlink's basename. These names are stable across reboots;
// by-id names are also stable across USB port moves (descriptor-based),
// by-path names are not (topology-based). Returns empty when the directory
// doesn't exist or no entry matches.
std::string find_udev_link(const std::string& dir, const std::string& card_num) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) {
        return "";
    }
    std::string result;
    while (auto* entry = ::readdir(d)) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        char buf[PATH_MAX];
        const std::string link = dir + "/" + entry->d_name;
        const ssize_t n = ::readlink(link.c_str(), buf, sizeof(buf) - 1);
        if (n <= 0) {
            continue;
        }
        buf[n] = '\0';
        if (node_belongs_to_card(basename_of(buf), card_num)) {
            result = entry->d_name;
            break;
        }
    }
    ::closedir(d);
    return result;
}

}  // namespace

std::string RealDeviceIdResolver::resolve(PaDeviceIndex /*index*/, const PaDeviceInfo& info) const {
    if (!info.name) {
        return "";
    }
    const std::string name = info.name;

    // Extract the ALSA card index from PortAudio's device name, e.g.
    // "USB PnP Sound Device: Audio (hw:1,0)" => 1. Virtual endpoints like
    // "default" or "pulse" will not match — they are not bound to specific
    // hardware so an empty id is the correct answer.
    static const std::regex hw_regex(R"(\(hw:(\d+)(?:,\d+)?\))");
    std::smatch m;
    if (!std::regex_search(name, m, hw_regex)) {
        return "";
    }
    const std::string card_num = m[1].str();

    // Prefer udev's descriptor-based symlink — stable across reboots and
    // USB port changes, and disambiguates identical devices when they
    // advertise serials.
    if (auto id = find_udev_link("/dev/snd/by-id", card_num); !id.empty()) {
        return "by-id:" + id;
    }
    // Fall back to the topology-based symlink — stable across reboots but
    // breaks if the device is moved to a different USB port. Still tells
    // two identical devices apart as long as neither is replugged.
    if (auto id = find_udev_link("/dev/snd/by-path", card_num); !id.empty()) {
        return "by-path:" + id;
    }
    // Final fallback for systems whose udev rules don't populate the above
    // (minimal containers, some embedded distros). The kernel-assigned
    // card id is driver-derived and stable on the same hardware topology.
    const std::string kernel_id = read_file("/sys/class/sound/card" + card_num + "/id");
    if (!kernel_id.empty()) {
        return "alsa-card:" + kernel_id;
    }
    VIAM_SDK_LOG(debug) << "[device_id] No stable id for ALSA card " << card_num << " (\"" << name << "\")";
    return "";
}

}  // namespace device_id
}  // namespace audio

#else

namespace audio {
namespace device_id {

std::string RealDeviceIdResolver::resolve(PaDeviceIndex, const PaDeviceInfo&) const {
    return "";
}

}  // namespace device_id
}  // namespace audio

#endif
