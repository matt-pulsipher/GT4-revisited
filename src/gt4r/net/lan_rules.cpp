#include "gt4r/net/lan_rules.h"

#include <optional>

namespace gt4r::net {

namespace {

// The number a routine sets at a place (a constant loaded into a register), as the disc's program has it.
std::optional<u8> constant(const sim::Program& program, u32 at) {
    const u32 w = program.w(at);
    const u32 op = w >> 26, from = (w >> 21) & 31;
    if ((op != 9 && op != 13) || from != 0) return std::nullopt;
    return u8(w & 0xFF);
}

} // namespace

std::array<u8, 9> lan_wear(const sim::Program& program, int tire_damage) {
    std::array<u8, 9> b{};
    if (!program.known() || tire_damage < 1 || tire_damage > 2) return b;
    auto c = [&](u32 at) { return constant(program, at).value_or(0); };
    if (tire_damage == 1) {
        const u8 five = c(0x354F58), four = c(0x354F5C), a2 = c(0x354F60), a3 = c(0x354F64), v1 = c(0x354F68);
        b = {a2, a3, v1, u8(tire_damage), five, four, 0, five, five};
    } else {
        const u8 last = c(0x354F4C), three = c(0x354F98), four = c(0x354F9C), a2 = c(0x354FA0), a3 = c(0x354FA4), v1 = c(0x354FA8);
        b = {a2, a3, v1, c(0x354F34), last, three, 0, four, last};
    }
    return b;
}

void lan_boost(const sim::Program& program, int boost, sim::ai::Pace& pace) {
    if (!program.known() || boost < 1 || boost > 2) return;
    auto c = [&](u32 at) { return constant(program, at).value_or(0); };
    pace.band_mode = 0;
    pace.slow_pct = pace.slow_start_m = pace.slow_full_10m = 0;
    if (boost == 1) pace.fast_pct = c(0x354EDC), pace.fast_start_m = c(0x354ED8), pace.fast_full_10m = c(0x354ED8);
    else pace.fast_pct = pace.fast_start_m = c(0x354EF8), pace.fast_full_10m = c(0x354ECC);
    pace.fast = float(pace.fast_pct) * 0.01f, pace.fast_start = float(pace.fast_start_m), pace.fast_full = float(pace.fast_full_10m) * 10.0f;
}

} // namespace gt4r::net
