#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): phase 7 (0x3F7EA8), each wheel's steer angle
// from the steering command, the steering offset that follows the wheel's slip direction (wheel + 0x1C,
// the steering mode's), the steering command's follower and the compliance steer of the lateral force.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim {
struct Spec;
}

namespace gt4r::sim::rules {

// The race's tables the phase reads.
struct SteerTables {
    // The steering offset's settings (0x845BF0 .. 0x845C0F), built by 0x3F7BE0 at the race's start (the conductor's
    // virtual 26) from the static block's bytes +0xED .. +0xF4.
    u32 mode = 0;                     // 0x845BF0 (+0xED; 0 when either limit's byte is 0): 1 the wheel's offset is added to its
                                      // steer angle for the slip angle and the forces, 2 it stands in its place
    u32 scale = 0;                    // 0x845BF4 (+0xEE): the wheel's peak force x the group's wheels over the axle's load
                                      // scales 1 the limit, 2 the spring, 3 both
    std::array<F, 2> limit;           // 0x845BF8 + 4 k: the byte +0xEF + k x 0x3AE4C385 (tenths of a degree in radians)
    std::array<F, 2> spring;          // 0x845C00 + 4 k: the byte +0xF1 + k x 10
    std::array<F, 2> damping;         // 0x845C08 + 4 k: the byte +0xF3 + k
    std::array<u8, 24> fixed_aim{};   // 0x620288 + surface: 1 where the offset aims at 0 (mode 2: at the steer angle)
    // The compliance steer (0x620B00 .. 0x620B5F), built by 0x366100 at the race's start from the bytes at 0x620AF0
    // (its call 0x34CC38: 4 and 2 for the tables' last x and y).
    u8 compliance_on = 0;             // 0x620AF0
    F compliance_max;                 // 0x620B04: the byte 0x620AF1 x 0.01 degrees, in radians
    F compliance_spring;              // 0x620B08: the byte 0x620AF2 x 100
    F compliance_damping;             // 0x620B0C: the byte 0x620AF3
    ex::Table<2> age_curve;           // 0x620B24: x the s16 at 0x620AF4 / 0x620AF6 x 1000, y 0 and 2: over the car's F + 0x5E8
    ex::Table<2> dead_band;           // 0x620B40: x 0 and 6, y the bytes 0x620AF8 / 0x620AF9 / 100
    ex::Table<2> compliance_gain;     // 0x620B54: x 0 and 6, y the bytes 0x620AFA / 0x620AFB x 0.01 degrees, in radians
    // A two-wheeler's steering by its lean (the table 0x6205C8, 0x78 bytes a row, its word +0x30; the row the parameter
    // block's byte +0x33 names, 0 past 2).
    std::array<F, 3> lean_gain;
};
constexpr u32 kSteerSettings = 0x845BF0, kFixedAim = 0x620288, kComplianceBytes = 0x620AF0, kCompliance = 0x620B00;
constexpr u32 kTwoWheelRows = 0x6205C8, kTwoWheelRowSize = 0x78;
// Built from the program's bytes as the race's start builds them.
SteerTables steer_tables(const Program& program);
// As the race's start left them in memory (`read(address)` a word, `byte(address)` a byte).
template <class W, class B> SteerTables steer_tables(W&& read, B&& byte) {
    SteerTables g;
    g.mode = read(kSteerSettings), g.scale = read(kSteerSettings + 4);
    for (u32 k = 0; k < 2; ++k) {
        g.limit[k] = F::raw(read(kSteerSettings + 8 + 4 * k)), g.spring[k] = F::raw(read(kSteerSettings + 0x10 + 4 * k));
        g.damping[k] = F::raw(read(kSteerSettings + 0x18 + 4 * k));
    }
    for (u32 s = 0; s < 24; ++s) g.fixed_aim[s] = byte(kFixedAim + s);
    g.compliance_on = byte(kComplianceBytes);
    const u32 C = kCompliance;
    g.compliance_max = F::raw(read(C + 4)), g.compliance_spring = F::raw(read(C + 8)), g.compliance_damping = F::raw(read(C + 0xC));
    auto table = [&](ex::Table<2>& t, u32 at) {
        t.n = gt3r::s16(read(at) & 0xFFFF);
        const u32 xs = read(at + 4), ys = read(at + 8);
        for (u32 k = 0; k < 2; ++k) t.x[k] = F::raw(read(xs + 4 * k)), t.y[k] = F::raw(read(ys + 4 * k));
    };
    table(g.age_curve, C + 0x24), table(g.dead_band, C + 0x40), table(g.compliance_gain, C + 0x54);
    for (u32 k = 0; k < 3; ++k) g.lean_gain[k] = F::raw(read(kTwoWheelRows + kTwoWheelRowSize * k + 0x30));
    return g;
}
// The tables' values equal, bit for bit.
bool same(const SteerTables& a, const SteerTables& b);

// What the phase keeps that the engine's record has no member for (the wheel's offset +0x1C is TireExtra's
// steer_offset).
struct SteerExtra {
    std::array<F, 2> toe;                // P + 0x1008 + 4 axle: subtracted on the left wheel, added on the right
    F rear_ratio;                        // P + 0x10CC: the rear wheels' share of the steering command (against it)
    u8 rear_steer = 0;                   // P + 0x10D0: 1 the wheels behind the center of gravity steer
    u8 lean_row = 0;                     // P + 0x33: a two-wheeler's row of 0x6205C8
    std::array<F, 2> axle_load;          // P + 0x1040 + 4 k: the sprung mass x 9.8 x the axle's weight share
    std::array<F, 2> compliance_base;    // P + 0x1050 + 4 k: the compliance's base (the dead band's and gain's x)
    std::array<F, 2> compliance_per_n;   // P + 0x1058 + 4 k: the axle's wheels over its load (the lateral force's share)
    std::array<F, 4> offset_rate;        // wheel + 0x18: the steering offset's rate
    std::array<F, 4> compliance;         // wheel + 0xCC: the compliance steer (added to the steer angle)
    std::array<F, 4> compliance_rate;    // wheel + 0xD0: its rate
    F follow, follow_rate, follow_accel; // F + 0x664 / + 0x668 / + 0x66C: a follower of 11.25 x the steering command
};
template <class X, class V> void steer_fields(X& x, V&& v) {
    using layout::Block;
    const Block P = Block::Params, C = Block::Car;
    for (int k = 0; k < 2; ++k) {
        v("steer.toe", k, P, 0x1008 + 4 * u32(k), x.toe[size_t(k)]);
        v("steer.axle_load", k, P, 0x1040 + 4 * u32(k), x.axle_load[size_t(k)]);
        v("steer.compliance_base", k, P, 0x1050 + 4 * u32(k), x.compliance_base[size_t(k)]);
        v("steer.compliance_per_n", k, P, 0x1058 + 4 * u32(k), x.compliance_per_n[size_t(k)]);
    }
    v("steer.rear_ratio", -1, P, 0x10CC, x.rear_ratio), v("steer.rear_steer", -1, P, 0x10D0, x.rear_steer);
    v("steer.lean_row", -1, P, 0x33, x.lean_row);
    for (int w = 0; w < 4; ++w) {
        const u32 W = layout::kWheel0 + layout::kWheelSize * u32(w);
        v("w.offset_rate", w, C, W + 0x18, x.offset_rate[size_t(w)]);
        v("w.compliance", w, C, W + 0xCC, x.compliance[size_t(w)]), v("w.compliance_rate", w, C, W + 0xD0, x.compliance_rate[size_t(w)]);
    }
    v("steer.follow", -1, C, layout::kState + 0x664, x.follow), v("steer.follow_rate", -1, C, layout::kState + 0x668, x.follow_rate);
    v("steer.follow_accel", -1, C, layout::kState + 0x66C, x.follow_accel);
}

// The converter's (0x361908) values of SteerExtra's parameter part, from the spec and the parameter block as the
// converter leaves it: the toe (atan2 of the spec's bytes +0x8E / +0x8F, less 0x80, x 0.0005, over the tire's radius),
// the rear steering (+0x2C, +0x2D x 0.01), the two-wheeler's row (+0x23), the axles' loads (the mass less the wheels'
// over each axle's share, x 9.8), the compliance's base (+0x38 / +0x39) and the axles' wheels over their loads (or over
// the spec's s16 +0x3A x 9.8 x the share where it is not 0).
void steer_params(SteerExtra& x, const Spec& s, const ex::Params& p, const BodyExtra& body, const WheelLayout& l);

// 0x3F7CA8 (on the parameter block): wheel w's steer angle for the steering command `delta` (its second float argument is
// not read): a wheel ahead of the center of gravity by its ratio (side 0 / 1: the ratio of its side when delta >= 0, else of
// the other side; a center wheel the command itself), one behind it the command against it x the rear ratio x the same
// side ratio when the rear wheels steer (else 0); then the toe of its axle, taken off on the left, added on the right.
F wheel_angle(const ex::Car& c, const SteerExtra& x, const WheelLayout& l, int w, F delta);
// 0x3F7E20: a two-wheeler's wheel ahead of the center of gravity: the steering command x 0x35A008 (cos (lean gain) x
// (3 - 2 cos (roll - the ground's roll))); 0 for the others.
F lean_angle(const ex::Car& c, const SteerExtra& x, const WheelLayout& l, const SteerTables& g, int w);
// 0x40CC58: the steering command's follower: a = 200 (11.25 x command - x) - 20 v, v += a / 60, x += v / 60.
void follow_steer(const ex::Car& c, SteerExtra& x);
// 0x36CC40: the wrapped difference a - b (0x36CB90 wraps each into [-pi, pi)).
F angle_diff(F a, F b);
// 0x36CCD8: wa a + wb b with b or a a turn on where they are more than half a turn apart, wrapped.
F angle_blend(F wa, F a, F wb, F b);
// The steering offset's aim for wheel w (0x3F7EA8's first loop, before the spring). Mode 1: 0 on a surface marked in
// 0x620288; at rest (the slip direction's fade 0) the offset less half the steer angle's change since the last step, within
// the limit; rolling backwards half the wrapped difference of the slip direction turned half a turn and the steer angle
// (0x36CC40); else half the slip direction less the steer angle. Mode 2: the steer angle on a marked surface, the offset
// itself at rest, the two angles' mean (0x36CCD8, wrapped) backwards, else half their sum.
F offset_aim(const ex::Car& c, const TireExtra& t, const WheelLayout& l, const SteerTables& g, int w);
// The steering offset for wheel w: a spring and damper toward its aim, held within the limit (around the steer angle
// under mode 2).
void steer_offset(ex::Car& c, SteerExtra& x, TireExtra& t, const WheelLayout& l, const SteerTables& g, int w);
// The compliance steer for wheel w: the lateral force (curve A) x the axle's share, less the dead band, x the gain (both
// tables read at the axle's base plus the age curve at F + 0x5E8), within the limit (turned on the rear axle), through a
// spring and damper; added to the steer angle.
void compliance_steer(ex::Car& c, SteerExtra& x, const WheelLayout& l, const SteerTables& g, int w);

// 0x3F7EA8, the phase: each wheel's steer angle (0x3F7CA8, a two-wheeler's 0x3F7E20), the follower (0x40CC58), each
// wheel's steering offset (under a steering mode) and compliance steer (when the compliance is on).
void wheel_steer(ex::Car& c, SteerExtra& x, TireExtra& t, const WheelLayout& l, const SteerTables& g);

} // namespace gt4r::sim::rules
