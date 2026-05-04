#pragma once

#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <string>

namespace audio {
namespace utils {

struct AlsaHw {
    int card_num;
    int device_num;
};

// Extracts (card, device) from a PortAudio ALSA device name, e.g.
// "USB PnP Sound Device: Audio (hw:1,0)" → {"1", 0}. Returns nullopt for
// names without an "(hw:X,Y)" suffix (virtual endpoints like "default"
// or non-Linux backends).
inline std::optional<AlsaHw> parse_alsa_hw(const std::string& name) {
    static const std::regex hw_regex(R"(\(hw:(\d+),(\d+)\))");
    std::smatch m;
    if (!std::regex_search(name, m, hw_regex)) {
        return std::nullopt;
    }
    return AlsaHw{std::stoi(m[1].str()), std::stoi(m[2].str())};
}

inline std::string trim(const std::string& s) {
    const size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) {
        return "";
    }
    const size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Reads the whole file at `path` and returns its contents with leading and
// trailing whitespace stripped. Returns "" on open failure. Suited for tiny
// sysfs/proc files; do not use on large inputs.
inline std::string read_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) {
        return "";
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return trim(ss.str());
}

}  // namespace utils
}  // namespace audio
