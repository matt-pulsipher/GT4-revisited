#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the suspension and the body's attitude, the
// first four routines of the chassis phase (0x344530): 0x36A490, 0x36A758, 0x36B0C8, 0x36BA88.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_chassis.h"

namespace gt4r::sim::rules {

// The program's switches and constants these routines read.
struct BodyGlobals {
    u8 lateral_geometry = 1; // 0x620E11 (static block +0x139): each wheel's lateral force about its roll centre, not the mass's
    u8 normal_mode = 2;      // 0x620DEC (+0x114): 2 the wheels push along their ground's normal, 1 along the ground's up axis, 0 the body's
    u8 damper_linear = 0;    // 0x620DD9 (+0x101): the dampers' linear fit (block +0x18 .. +0x2C, +0x48 / +0x4C) instead of the root fit
    u32 damper_table = 0;    // 0x620CB0: not 0: the root fit eased into a square law at the fast end
    std::array<F, 2> bump_knee, bump_share, rebound_knee, rebound_share; // 0x620CB8 / CC0 / CC8 / CD0 + 4 axle
    std::array<F, 6> in_line;                                            // 0x62076C .. 0x620780: the in-line layout's roll terms
};
constexpr u32 kLateralGeometry = 0x620E11, kNormalMode = 0x620DEC, kDamperLinear = 0x620DD9, kDamperTable = 0x620CB0;
constexpr u32 kDamperKnees = 0x620CB8, kInLine = 0x62076C;
BodyGlobals body_globals(const Program& program);
// From the words and bytes at those addresses (`read(address)` a word, `byte(address)` a byte).
template <class W, class B> BodyGlobals body_globals(W&& read, B&& byte) {
    BodyGlobals g;
    g.lateral_geometry = byte(kLateralGeometry), g.normal_mode = byte(kNormalMode), g.damper_linear = byte(kDamperLinear);
    g.damper_table = read(kDamperTable);
    for (u32 a = 0; a < 2; ++a) {
        g.bump_knee[a] = F::raw(read(kDamperKnees + 4 * a)), g.bump_share[a] = F::raw(read(kDamperKnees + 8 + 4 * a));
        g.rebound_knee[a] = F::raw(read(kDamperKnees + 0x10 + 4 * a)), g.rebound_share[a] = F::raw(read(kDamperKnees + 0x18 + 4 * a));
    }
    for (u32 k = 0; k < 6; ++k) g.in_line[k] = F::raw(read(kInLine + 4 * k));
    return g;
}

// An axle's suspension block's fields past the engine's (P + 0x308 + 0x58 a).
struct AxleBody {
    F wheel_mass;        // +0x38 the wheel's own mass (its travel integrated with it)
    F hold_droop;        // +0x44 the droop while the hold counter runs
    F linear_bump_max;   // +0x48 the linear damper fit's force at full bump speed
    F linear_rebound_max;// +0x4C at full rebound speed
};

// What these routines keep that the engine's record has no member for.
struct BodyExtra {
    std::array<AxleBody, 2> axle;
    std::array<F, 2> tire_drop;    // P + 0x2DC + 4 a: taken off the tire's radius at the hub's rest point
    F front_share;                 // P + 0x24: the front axle's share of the weight
    F roll_centre;                 // P + 0x1060: added to each wheel's lever for its lateral force
    u8 speed_part = 0;             // P + 0x1076: the part that changes with speed's damping row (0x3F2650; not ported)
    u8 speed_part_kind = 0;        // P + 0x1077 (7: a lift under the body near the ground, P + 0x1084)
    F speed_part_lift;             // P + 0x1084
    F sprung_mass;                 // F + 0x4D8: the mass the heave and its forces are divided by
    F in_line_lean;                // F + 0x5B8 (the in-line layout): the lateral acceleration x mass x up z
    F in_line_x480, in_line_x65c;  // F + 0x480 / + 0x65C (the in-line layout's roll terms) ❓
    std::array<F, 4> contact_up;   // wheel + 0xC8: the ground's normal . the body's up axis
    std::array<F, 4> normal_speed; // wheel + 0xD8: the hub's speed along the ground's normal
    std::array<u8, 4> contact{};   // wheel + 0xDC: 0 off the ground, 1 on it, 2 landed this step, 3 left it this step
};
template <class X, class V> void body_fields(X& x, V&& v) {
    using layout::Block;
    const Block P = Block::Params, C = Block::Car;
    for (int a = 0; a < 2; ++a) {
        const u32 S = 0x308 + 0x58 * u32(a);
        auto& s = x.axle[size_t(a)];
        v("body.wheel_mass", a, P, S + 0x38, s.wheel_mass), v("body.hold_droop", a, P, S + 0x44, s.hold_droop);
        v("body.linear_bump_max", a, P, S + 0x48, s.linear_bump_max), v("body.linear_rebound_max", a, P, S + 0x4C, s.linear_rebound_max);
        v("body.tire_drop", a, P, 0x2DC + 4 * u32(a), x.tire_drop[size_t(a)]);
    }
    v("body.front_share", -1, P, 0x24, x.front_share), v("body.roll_centre", -1, P, 0x1060, x.roll_centre);
    v("body.speed_part", -1, P, 0x1076, x.speed_part), v("body.speed_part_kind", -1, P, 0x1077, x.speed_part_kind);
    v("body.speed_part_lift", -1, P, 0x1084, x.speed_part_lift);
    v("body.sprung_mass", -1, C, layout::kState + 0x4D8, x.sprung_mass);
    v("body.in_line_lean", -1, C, layout::kState + 0x5B8, x.in_line_lean);
    v("body.in_line_x480", -1, C, layout::kState + 0x480, x.in_line_x480), v("body.in_line_x65c", -1, C, layout::kState + 0x65C, x.in_line_x65c);
    for (int w = 0; w < 4; ++w) {
        const u32 W = layout::kWheel0 + layout::kWheelSize * u32(w);
        v("w.contact_up", w, C, W + 0xC8, x.contact_up[size_t(w)]), v("w.normal_speed", w, C, W + 0xD8, x.normal_speed[size_t(w)]);
        v("w.contact", w, C, W + 0xDC, x.contact[size_t(w)]);
    }
}

// 0x36A490: the body's up axis against the ground's (F + 0x5B4: their dot product, into the engine's
// `terrain`), each wheel's ground normal against it (wheel + 0xC8), the suspension's places (0x36A080) and
// the mask of the wheels past their bump stop (0x36A1D8).
void terrain(ex::Car& c, BodyExtra& x, const WheelLayout& l);
// 0x36A080: each wheel's place along its travel from the hub's height over its ground: the hub's rest
// point below it (0x3696C8 / 0x369760), the height over the ground scaled by the ground's up z over the
// wheel's normal . up.
void susp_coords(ex::Car& c, const BodyExtra& x, const WheelLayout& l);
// 0x36A1D8: `mask` with the bits of the wheels past their bump stop.
u32 bump_mask(const ex::Car& c, const WheelLayout& l, u32 mask);
// 0x369B48: an axle's damper force at a travel speed.
F damper(const ex::Suspension& s, const AxleBody& b, const BodyGlobals& g, F v, u32 axle);
// 0x36A758: each wheel's normal speed, spring and damper (0x369DE8: the wheel's travel integrated with its
// own mass; off the ground when it would stand above it), contact state, half track (0x36A690) and
// camber (0x36A578); the anti-roll bars by wheel group (0x36A048); each wheel's body force with the
// longitudinal and lateral load transfers and the downforce.
void suspension(ex::Car& c, BodyExtra& x, const ChassisExtra& ch, const WheelLayout& l, const BodyGlobals& g);
// 0x36AF30: the tires' longitudinal forces' pitch moment and lift.
void pitch_terms(const ex::Car& c, const WheelLayout& l, F& moment, F& lift);
// 0x36B0C8 without its start hold (0x36AE20: the caller's while the hold counter runs): pitch and roll from
// the body forces and dampers (the roll rate within 4 pi), the body axes (0x36C9F8), the heave: the
// wheels' loads along their ground's normals, the lift and the downforce over the sprung mass.
void attitude(ex::Car& c, BodyExtra& x, const ChassisExtra& ch, const TireExtra& t, const WheelLayout& l, const BodyGlobals& g);
// 0x36BA88: a wheel past its bump stop lifts and turns the body back over it, as the engine's but on the
// wheel layout; the body axes; the downward speed held to the ground plane's.
void bump_nudge(ex::Car& c, const BodyExtra& x, const WheelLayout& l);

} // namespace gt4r::sim::rules
