#include <gtest/gtest.h>

#include <cstring>

#include "portaudio.h"
#include "routing_filter.hpp"

using audio::routing::ApeRoutingMap;
using audio::routing::CardRouting;
using audio::routing::is_unrouted_admaif;
using audio::routing::parse_admaif_index;

// --- parse_admaif_index ----------------------------------------------------

TEST(ParseAdmaifIndex, SingleDigit) {
    EXPECT_EQ(parse_admaif_index("ADMAIF1"), 1);
}

TEST(ParseAdmaifIndex, MultiDigit) {
    EXPECT_EQ(parse_admaif_index("ADMAIF12"), 12);
}

TEST(ParseAdmaifIndex, FollowedBySuffix) {
    // The real input is e.g. "ADMAIF3 Mux"; the function only requires the
    // prefix + a digit at `pos` and is happy to stop at non-digit boundaries
    // because std::stoi parses up to the first non-digit.
    EXPECT_EQ(parse_admaif_index("ADMAIF3 Mux"), 3);
}

TEST(ParseAdmaifIndex, MissingPrefix) {
    EXPECT_EQ(parse_admaif_index("MIXER1"), -1);
}

TEST(ParseAdmaifIndex, PrefixWithoutDigit) {
    EXPECT_EQ(parse_admaif_index("ADMAIF Mux"), -1);
}

TEST(ParseAdmaifIndex, PrefixOnly) {
    EXPECT_EQ(parse_admaif_index("ADMAIF"), -1);
}

TEST(ParseAdmaifIndex, EmptyString) {
    EXPECT_EQ(parse_admaif_index(""), -1);
}

TEST(ParseAdmaifIndex, Offset) {
    // pos lets callers skip past a known prefix in a longer string.
    EXPECT_EQ(parse_admaif_index("XBAR-ADMAIF7", 5), 7);
}

TEST(ParseAdmaifIndex, OffsetMisalignedReturnsMismatch) {
    // pos pointing at "DMAIF7" — missing the leading 'A', so prefix doesn't match.
    EXPECT_EQ(parse_admaif_index("XBAR-ADMAIF7", 6), -1);
}

// --- is_unrouted_admaif ---------------------------------------------------

namespace {

// Build a PaDeviceInfo with just the bits is_unrouted_admaif looks at.
PaDeviceInfo make_info(const char* name) {
    PaDeviceInfo info{};
    info.name = name;
    return info;
}

ApeRoutingMap make_routing(int card_num,
                           std::initializer_list<int> inputs,
                           std::initializer_list<int> outputs) {
    CardRouting card;
    for (int n : inputs) {
        card.routed_inputs.insert(n);
    }
    for (int n : outputs) {
        card.routed_outputs.insert(n);
    }
    ApeRoutingMap m;
    m.emplace(card_num, std::move(card));
    return m;
}

}  // namespace

TEST(IsUnroutedAdmaif, NullDeviceNameIsNotFiltered) {
    const auto routing = make_routing(1, {1, 2, 3}, {});
    PaDeviceInfo info{};
    info.name = nullptr;
    EXPECT_FALSE(is_unrouted_admaif(info, true, routing));
}

TEST(IsUnroutedAdmaif, NonAlsaNameIsNotFiltered) {
    // No "(hw:X,Y)" suffix → parse_alsa_hw returns nullopt → don't filter.
    const auto routing = make_routing(1, {1}, {});
    const auto info = make_info("default");
    EXPECT_FALSE(is_unrouted_admaif(info, true, routing));
}

TEST(IsUnroutedAdmaif, CardNotInRoutingMapIsNotFiltered) {
    // USB sound card on hw:2 with empty map for that card → not Tegra APE,
    // so leave it alone.
    const ApeRoutingMap routing;  // empty
    const auto info = make_info("USB Audio (hw:2,0)");
    EXPECT_FALSE(is_unrouted_admaif(info, true, routing));
    EXPECT_FALSE(is_unrouted_admaif(info, false, routing));
}

TEST(IsUnroutedAdmaif, RoutedInputIsKept) {
    // PCM device 0 → ADMAIF1. ADMAIF1 is in routed_inputs → not unrouted.
    const auto routing = make_routing(1, {1, 2}, {});
    const auto info = make_info("APE (hw:1,0)");
    EXPECT_FALSE(is_unrouted_admaif(info, true, routing));
}

TEST(IsUnroutedAdmaif, UnroutedInputIsFiltered) {
    // PCM device 4 → ADMAIF5. ADMAIF5 is NOT in routed_inputs → filter.
    const auto routing = make_routing(1, {1, 2, 3}, {});
    const auto info = make_info("APE (hw:1,4)");
    EXPECT_TRUE(is_unrouted_admaif(info, true, routing));
}

TEST(IsUnroutedAdmaif, RoutedOutputIsKept) {
    // PCM device 2 → ADMAIF3. ADMAIF3 in routed_outputs → not unrouted.
    const auto routing = make_routing(1, {}, {3, 7});
    const auto info = make_info("APE (hw:1,2)");
    EXPECT_FALSE(is_unrouted_admaif(info, false, routing));
}

TEST(IsUnroutedAdmaif, UnroutedOutputIsFiltered) {
    const auto routing = make_routing(1, {}, {1});
    const auto info = make_info("APE (hw:1,2)");  // ADMAIF3 not in outputs
    EXPECT_TRUE(is_unrouted_admaif(info, false, routing));
}

TEST(IsUnroutedAdmaif, InputOutputAreIndependent) {
    // ADMAIF5 is a routed input but NOT a routed output. Asking the input
    // direction should keep it; asking the output direction should filter it.
    const auto routing = make_routing(1, {5}, {1, 2});
    const auto info = make_info("APE (hw:1,4)");  // device 4 → ADMAIF5
    EXPECT_FALSE(is_unrouted_admaif(info, true, routing));
    EXPECT_TRUE(is_unrouted_admaif(info, false, routing));
}

TEST(IsUnroutedAdmaif, MultiDigitDeviceNumber) {
    // device 18 → ADMAIF19. Real Orin Nano case.
    const auto routing = make_routing(1, {19}, {});
    const auto info = make_info("APE: - (hw:1,18)");
    EXPECT_FALSE(is_unrouted_admaif(info, true, routing));

    const auto unrouted_info = make_info("APE: - (hw:1,19)");  // → ADMAIF20
    EXPECT_TRUE(is_unrouted_admaif(unrouted_info, true, routing));
}
