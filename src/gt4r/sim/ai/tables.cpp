#include "gt4r/sim/ai/tables.h"

#include <algorithm>

namespace gt4r::sim::ai {

using gt3r::ee::F;

namespace {

// Addresses in SCUS-97328 v1.01 (docs/specs/ai.md "The program's tables").
constexpr u32 kTuningBytes = 0x623FB8; // the bytes the tuning block is built from
constexpr u32 kBuiltinSets = 0x623DC0; // two driver sets of 0x40 bytes
constexpr u32 kFallback = 0x624030;    // 8 rows of 6
constexpr u32 kOrder = 0x624018;       // 8 bytes
constexpr u32 kDriverSpread = 0x620DE9, kSaturationCut = 0x620DE7;

} // namespace

// 0x359568 on a table of two points: y0 at x0 and below, y1 at x1 and above, linear between.
F Ramp::at(F x) const {
    if (x <= x0) return y0;
    if (x1 <= x) return y1;
    return y0 + (y1 - y0) * (x - x0) / (x1 - x0);
}

Tables Tables::load(const Program& program) {
    Tables t;
    if (!program.known()) {
        // Neutral: a requested passing line falls back to the main line (6), the main line to either passing line.
        for (auto& row : t.fallback) row.fill(-1);
        const s8 rows[8][4] = {{-1}, {1, 6, 2, -1}, {2, 6, 1, -1}, {3, -1}, {4, -1}, {-1}, {6, 1, 2, -1}, {7, 6, -1}};
        for (size_t r = 0; r < 8; ++r)
            for (size_t k = 0; k < 4; ++k) t.fallback[r][k] = rows[r][k];
        return t;
    }
    t.known = true;
    const auto S = [&](u32 i) { return program.b(kTuningBytes + i); };
    // 0x468EE8 (and 0x469468 for the spreads' switches), in the program's arithmetic.
    const F tenth = F::raw(0x3DCCCCCC), hundredth = F::raw(0x3C23D70A), hundred = F::raw(0x42C80000);
    const auto I = [&](u32 i) { return gt3r::ee::int_to_f(s32(S(i))); };
    Tuning& u = t.tuning;
    u.smooth_throttle = S(0);
    u.steer_mode = S(2);
    u.pd[0] = I(0x09), u.pd[1] = I(0x0A);
    for (u32 k = 0; k < 4; ++k) u.pd[2 + k] = I(0x0B + k) * tenth;
    for (u32 k = 0; k < 7; ++k) u.pd[7 + k] = I(0x10 + k) * tenth;
    u.pd_bound = I(0x1C) * hundredth;
    u.blend_by_distance = S(0x24);
    u.blend_rate = I(0x25) * tenth;
    u.offset_flags = u8((S(0x28) ? 1 : 0) | (S(0x29) ? 2 : 0) | (S(0x2A) ? 4 : 0));
    u.side_share = S(0x28) ? I(0x2B) * tenth : F(), u.side_share_pit = S(0x28) ? I(0x2C) * tenth : F();
    u.offset_rate = u.offset_flags ? I(0x2D) * tenth : F(), u.offset_limit = u.offset_flags ? I(0x2E) * tenth : F();
    u.offset_by_grip = S(0x30);
    u.grip_lo = u.offset_by_grip ? I(0x31) * hundredth : F(), u.grip_hi = u.offset_by_grip ? I(0x32) * hundredth : F();
    u.alongside_slow = S(0x34);
    u.alongside_share = I(0x35) * hundredth;
    u.corner_spread = S(0x38);
    u.corner_lo = {I(0x39) / hundred, I(0x3A) / hundred, (-I(0x3B)) / hundred, (-I(0x3C)) / hundred};
    u.corner_hi = {I(0x39) / hundred, I(0x3A) / hundred, I(0x3D) / hundred, I(0x3E) / hundred};
    u.brake_spread = S(0x3F);
    u.brake_lo = {I(0x40) / hundred, I(0x41) / hundred, (-I(0x42)) / hundred, (-I(0x43)) / hundred};
    u.brake_hi = {I(0x40) / hundred, I(0x41) / hundred, I(0x44) / hundred, I(0x45) / hundred};
    u.brake_global = I(0x46) / hundred;
    for (u32 c = 0; c < 4; ++c) u.class_bonus[c] = S(0x20 + c);
    for (u32 s = 0; s < 2; ++s)
        for (u32 i = 0; i < 0x40; ++i) t.builtin[s][i] = program.b(kBuiltinSets + 0x40 * s + i);
    for (u32 r = 0; r < 8; ++r)
        for (u32 k = 0; k < 6; ++k) t.fallback[r][k] = s8(program.b(kFallback + 6 * r + k));
    for (u32 i = 0; i < 8; ++i) t.order[i] = std::min<u8>(program.b(kOrder + i), 7);
    // 0x3F1790: the pace levels.
    for (u32 i = 0; i < 5; ++i) {
        Tables::PaceLevel& l = t.pace[i];
        l.shift = u8((s32(s8(program.b(0x622088 + i))) * 128 + 0x3200) / 100);
        l.brake = gt3r::ee::int_to_f(program.b(0x622090 + i)) / hundred;
        l.corner = gt3r::ee::int_to_f(program.b(0x622098 + i)) / hundred;
        l.x20E0 = gt3r::ee::int_to_f(s32(s8(program.b(0x6220A0 + i))) + 100) / hundred;
    }
    t.pace_start = {std::min<u8>(program.b(0x622080), 4), std::min<u8>(program.b(0x622081), 4)};
    t.driver_spread = program.b(kDriverSpread);
    t.saturation_cut = program.b(kSaturationCut);
    return t;
}

} // namespace gt4r::sim::ai
