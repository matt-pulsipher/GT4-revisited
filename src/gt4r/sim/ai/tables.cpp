#include "gt4r/sim/ai/tables.h"

#include <algorithm>

namespace gt4r::sim::ai {

namespace {

// Addresses in SCUS-97328 v1.01 (docs/specs/ai.md "The program's tables").
constexpr u32 kTuningBytes = 0x623FB8; // the bytes the tuning block is built from
constexpr u32 kBuiltinSets = 0x623DC0; // two driver sets of 0x40 bytes
constexpr u32 kFallback = 0x624030;    // 8 rows of 6
constexpr u32 kOrder = 0x624018;       // 8 bytes
constexpr u32 kDriverSpread = 0x620DE9, kSaturationCut = 0x620DE7;

} // namespace

float Ramp::at(float x) const {
    if (x <= x0) return y0;
    if (x >= x1) return y1;
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
    Tuning& u = t.tuning;
    u.smooth_throttle = S(0);
    u.steer_mode = S(2);
    u.blend_by_distance = S(0x24);
    u.blend_rate = float(S(0x25)) * 0.1f;
    u.offset_flags = u8((S(0x28) ? 1 : 0) | (S(0x29) ? 2 : 0) | (S(0x2A) ? 4 : 0));
    u.side_share = float(S(0x2B)) * 0.1f, u.side_share_pit = float(S(0x2C)) * 0.1f;
    u.offset_rate = float(S(0x2D)) * 0.1f, u.offset_limit = float(S(0x2E)) * 0.1f;
    u.offset_by_grip = S(0x30);
    u.grip_lo = float(S(0x31)) * 0.01f, u.grip_hi = float(S(0x32)) * 0.01f;
    u.alongside_slow = S(0x34);
    u.alongside_share = float(S(0x35)) * 0.01f;
    u.corner_spread = S(0x38);
    u.corner_lo = {float(S(0x39)) * 0.01f, float(S(0x3A)) * 0.01f, -float(S(0x3B)) * 0.01f, -float(S(0x3C)) * 0.01f};
    u.corner_hi = {float(S(0x39)) * 0.01f, float(S(0x3A)) * 0.01f, float(S(0x3D)) * 0.01f, float(S(0x3E)) * 0.01f};
    u.brake_spread = S(0x3F);
    u.brake_lo = {float(S(0x40)) * 0.01f, float(S(0x41)) * 0.01f, -float(S(0x42)) * 0.01f, -float(S(0x43)) * 0.01f};
    u.brake_hi = {float(S(0x40)) * 0.01f, float(S(0x41)) * 0.01f, float(S(0x44)) * 0.01f, float(S(0x45)) * 0.01f};
    u.brake_global = float(S(0x46)) * 0.01f;
    for (u32 c = 0; c < 4; ++c) u.class_bonus[c] = S(0x20 + c);
    for (u32 s = 0; s < 2; ++s)
        for (u32 i = 0; i < 0x40; ++i) t.builtin[s][i] = program.b(kBuiltinSets + 0x40 * s + i);
    for (u32 r = 0; r < 8; ++r)
        for (u32 k = 0; k < 6; ++k) t.fallback[r][k] = s8(program.b(kFallback + 6 * r + k));
    for (u32 i = 0; i < 8; ++i) t.order[i] = std::min<u8>(program.b(kOrder + i), 7);
    t.driver_spread = program.b(kDriverSpread);
    t.saturation_cut = program.b(kSaturationCut);
    return t;
}

} // namespace gt4r::sim::ai
