#include "gt4r/net/lan_rules.h"

namespace gt4r::net {

namespace {

// Gran Turismo 4's constants as its routines load them (SCUS-97328 v1.01; the disc test lan_rules_equal_the_program
// reads them from the program's code).
// The tire wear (0x354F30): setting 1's constants 0x354F58 .. 0x354F68, setting 2's 0x354F34, 0x354F4C, 0x354F98 .. 0x354FA8.
constexpr std::array<u8, 9> kWear1 = {8, 40, 12, 1, 5, 4, 0, 5, 5};
constexpr std::array<u8, 9> kWear2 = {6, 40, 12, 1, 5, 3, 0, 4, 5};
// The boost (0x354EB8): setting 1's percent 0x354EDC and gap 0x354ED8 (m, and its full in 10 m); setting 2's 0x354EF8
// (percent and gap) and 0x354ECC (its full in 10 m).
constexpr u8 kBoost1Pct = 7, kBoost1Gap = 20, kBoost2 = 10, kBoost2Full = 20;

} // namespace

std::array<u8, 9> lan_wear(const sim::Program& program, int tire_damage) {
    if (!program.known() || tire_damage < 1 || tire_damage > 2) return {};
    return tire_damage == 1 ? kWear1 : kWear2;
}

void lan_boost(const sim::Program& program, int boost, sim::ai::Pace& pace) {
    if (!program.known() || boost < 1 || boost > 2) return;
    pace.band_mode = 0;
    pace.slow_pct = pace.slow_start_m = pace.slow_full_10m = 0;
    if (boost == 1) pace.fast_pct = kBoost1Pct, pace.fast_start_m = kBoost1Gap, pace.fast_full_10m = kBoost1Gap;
    else pace.fast_pct = pace.fast_start_m = kBoost2, pace.fast_full_10m = kBoost2Full;
    pace.fast = float(pace.fast_pct) * 0.01f, pace.fast_start = float(pace.fast_start_m), pace.fast_full = float(pace.fast_full_10m) * 10.0f;
}

} // namespace gt4r::net
