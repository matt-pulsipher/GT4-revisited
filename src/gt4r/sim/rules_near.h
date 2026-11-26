#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): which cars are near which and the slipstream
// (phase 14, 0x3F4638), and the part that changes with speed (phase 15, 0x3F28B8) with its damping (0x3F2650,
// in the suspension's step).

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"

#include <span>

namespace gt4r::sim {
struct Spec;
}

namespace gt4r::sim::rules {

// What these routines keep that the engine's record has no member for. The computer driver's object sits in the car's
// record at car + 0x14 (layout::kDriver); phase 14 writes these of its fields on every car, a player's too.
struct NearExtra {
    u8 driver_line = 0;               // car + 0x30 (driver +0x1C): the driver's line; 4 leaves the car out (0x463BA8)
    F contact;                        // car + 0x58 (driver +0x44): the contact with a car ahead (0x469918): 0 .. 2 (1 when it
                                      // takes a car to follow)
    std::array<u8, 8> zone{};         // car + 0x5C .. (driver +0x48 ..): the zone cells (0x4694F8): front, front left, front
                                      // right, left, right, rear left, rear right, rear; 2 a car there, 1 one partly there
    u8 follow = 0xFF;                 // car + 0x65 (driver +0x51): the index of the car followed (0xFF none)
    u8 threat = 0xFF;                 // car + 0x67 (driver +0x53): the index of the car closing soonest (0x469888)
    F gap_left, gap_right;            // car + 0x6C / + 0x70 (driver +0x58 / +0x5C): the least side gaps to a car alongside
    F threat_gap, threat_rate;        // car + 0x74 / + 0x78 (driver +0x60 / +0x64): that car's gap and closing speed
    u8 chain = 0;                     // car + 0x7E (driver +0x6A): the length of the follow chain from this car
    F course_rate;                    // F + 0x650: the change of the distance along the course a step, x 60 (the race's
                                      // progress, 0x358608)
    // The part that changes with speed (phase 15).
    F speed_lo, speed_hi;             // P + 0x1078 / + 0x107C: the speeds it changes at (m/s)
    F speed_rate;                     // P + 0x1080: how fast its state moves (a second)
    u8 speed_state = 0;               // F + 0x592: kinds 1, 4, 5: 1 out; kind 6: 1 out, 2 fully out
    u8 speed_state2 = 0;              // F + 0x593: kind 4's second state, by the lateral acceleration: 1 right, 2 left ❓
    F speed_part_state2;              // F + 0x598: kind 4's second state's value (-1 .. 1)
};
template <class X, class V> void near_fields(X& x, V&& v) {
    using layout::Block;
    const Block P = Block::Params, C = Block::Car;
    const u32 D = layout::kDriver;
    v("near.driver_line", -1, C, D + 0x1C, x.driver_line), v("near.contact", -1, C, D + 0x44, x.contact);
    for (int k = 0; k < 8; ++k) v("near.zone", k, C, D + 0x48 + u32(k), x.zone[size_t(k)]);
    v("near.follow", -1, C, D + 0x51, x.follow), v("near.threat", -1, C, D + 0x53, x.threat);
    v("near.gap_left", -1, C, D + 0x58, x.gap_left), v("near.gap_right", -1, C, D + 0x5C, x.gap_right);
    v("near.threat_gap", -1, C, D + 0x60, x.threat_gap), v("near.threat_rate", -1, C, D + 0x64, x.threat_rate);
    v("near.chain", -1, C, D + 0x6A, x.chain);
    v("near.course_rate", -1, C, layout::kState + 0x650, x.course_rate);
    v("speed_part.lo", -1, P, 0x1078, x.speed_lo), v("speed_part.hi", -1, P, 0x107C, x.speed_hi);
    v("speed_part.rate", -1, P, 0x1080, x.speed_rate);
    v("speed_part.state", -1, C, layout::kState + 0x592, x.speed_state);
    v("speed_part.state2", -1, C, layout::kState + 0x593, x.speed_state2);
    v("speed_part.value2", -1, C, layout::kState + 0x598, x.speed_part_state2);
}

// ---- phase 14 ----------------------------------------------------------------------------------------------------

// The race's tables of the cars' pairs (in the conductor, cleared by its virtual 27, 0x3F4478): for car i and car j,
// slip[i][j] how much car i takes of car j's slipstream (0x3F4428 / 0x3F4450) and code[i][j] where car j is from car i
// (0x3F4080 / 0x3F40A8: the low four bits along, the high four across; 0 not near). The race's pass-by states
// (0x4146E8) read both.
struct NearPairs {
    static constexpr int kCars = 6;
    std::array<std::array<F, kCars>, kCars> slip{};
    std::array<std::array<u8, kCars>, kCars> code{};
};
constexpr u32 kSlipTable = 0xCC14, kCodeTable = 0xCCA4; // conductor + (0x10000 - 0x33EC, 0x10000 - 0x335C)
constexpr u32 kRuleKind = 0xF81A;                        // conductor + (0x10000 - 0x7E6): the rule block's byte +0x02

// A car of the race as phase 14 sees it: its record and its part of NearExtra.
struct NearCar {
    ex::Car* car;
    NearExtra* x;
};

// 0x343BF0: a car left out of the others' company (and of car contact in the collision step): not in the race (F + 0x441)
// or its driver on line 4 (0x463BA8).
bool left_out(const ex::Car& c, const NearExtra& x);
// 0x3F40F8: car b seen from car a. False when either is left out, one is on the pit lane and the other not (F + 0x4B1
// bit 4), they are more than 100 m apart or b is lower than a by more than b's body height + 5 m, or higher by a's + 5 m.
// Else, with d = b's position less a's: u_a / v_a d along a's forward and lateral axes, u_b / v_b -d along b's; `dist`
// |d| (set once the pit lane test passes).
struct PairView {
    F u_a, v_a, u_b, v_b, dist;
};
bool pair_view(const ex::Car& a, const NearExtra& xa, const ex::Car& b, const NearExtra& xb, PairView& out);
// 0x3F4520: where a car at (u, v) is from one whose extents along it are `ahead` and `behind` and whose half width is
// `w`, `dist` apart: 0 past 50 m; else the low bits along (6 past 4 x ahead, 5 past ahead, 4 ahead, 3 alongside, 2 behind,
// 1 past 4 x behind), the high bits across (0x10 past 4 w on the left .. 0x70 past 4 w on the right).
u8 position_code(F dist, F u, F v, F ahead, F behind, F w);
// 0x3F44A0: how much car a, at (x, y) in leader b's frame, takes of b's slipstream: 1 - (-x) / (1.5 v) straight behind it
// within 1.5 x b's speed v in metres and within b's width either side (P + 0x08), with b above 100 km/h; else 0.
F slipstream(const ex::Car& b, F x, F y);
// 0x4694A0: the driver's part phase 14 makes anew: the contact and the zone cells cleared, the threat let go (its gap
// 1,000, its rate -1,000), the side gaps 1,000.
void near_reset(NearExtra& x);
// 0x4694F8: the zone cells of a car with another `dist` away at (u, v) in its frame; `ahead` / `behind` the two cars'
// extents along it, `w` their mean width.
void zones(NearExtra& x, F dist, F u, F v, F ahead, F behind, F w);
// 0x3F42D0: a closing on b ahead along the course: the gap along it (a lap `lap` added when a is past three quarters of it
// and b short of a quarter) less b's rear and a's front, the closing speed the difference of the two F + 0x650; false when
// the gap is not above 0, or within 20 m a car not overlapping a sideways (v: b across a). `japan`: SCPS-17001 v1.05's
// (0x3ECFE0), at any gap a car not overlapping a sideways.
bool closing(const ex::Car& a, const NearExtra& xa, const ex::Car& b, const NearExtra& xb, F lap, F v, F& gap, F& rate, bool japan = false);
// 0x469868: a lets go of b when it follows it.
void let_go(NearExtra& xa, const ex::Car& b);
// 0x469888: b as a's threat (the car closing soonest), with its gap and closing speed.
void threat(NearExtra& xa, const ex::Car& b, F gap, F rate);
// 0x469918: a's contact with b ahead, `gap` away closing at `rate` (and the threat first).
void contact(NearExtra& xa, const ex::Car& b, F gap, F rate);
// 0x3F4638, the phase: every car's driver part made anew; every pair i < j of the race's cars: near (pair_view), the zone
// cells, the codes, the contacts (unless the rule block's byte +0x02 is 6: the cars let go), the slipstreams; then each
// car's slipstream target (F + 0x59C, ex::Car::slip_target) the sum of its row of `pairs.slip`, at most 1, and each car's
// follow chain. `lap`: the course's lap length (0x34C230). `japan`: SCPS-17001 v1.05's phase (0x3ED338), the contacts only
// of cars less than 50 m apart (the others let go).
void proximity(std::span<const NearCar> cars, NearPairs& pairs, F lap, u8 rule_kind, bool japan = false);

// ---- phase 15 ----------------------------------------------------------------------------------------------------

// The converter's (0x361908, its aerodynamics part 0x3621B0) values of the part that changes with speed: its kind the
// spec's +0x4C; with a kind, the speeds +0x4D / +0x4E km/h, its rate 10 over +0x4F (300 when +0x4F is 0); all 0 without.
void speed_part_params(NearExtra& x, const Spec& s);
// 0x3F2798: the part's share at the speed v: 0 at or below P + 0x1078, 1 at or above P + 0x107C, linear between.
F speed_part_share(const NearExtra& x, F v);
// 0x3F28B8 (phase 15, on each car): by the part's kind (P + 0x1077, BodyExtra::speed_part_kind) its targets: 1 out above
// the upper speed and in below the lower (kind 1); the share (2); none (3); out while braking (the pedal at 0.5 or more)
// above the upper speed, in below the lower or with the pedal under 0.1 (5; kind 4 with a second state by the lateral
// acceleration over 9.8: past +-0.5 to +-1, back under +-0.2); kind 6: fully out braking from 10 km/h, 2/13 out above the
// upper speed, in below the lower, from fully out in with the pedal under 0.1 (to 2/13 at the lower speed or above) or below
// 2 km/h. The states (F + 0x594, ChassisExtra::speed_part_state, and kind 4's F + 0x598) move toward their targets at the
// rate P + 0x1080 a second (the second at twice it).
void speed_part(ex::Car& c, NearExtra& x, u8 kind, F& state);

// The damping rows of the part that changes with speed (0x622268, 0x20 bytes a row), built at the race's start (the
// conductor's virtual 26, 0x3F2390) from the program's bytes at 0x622240 (8 a row): five rows.
struct SpeedDampRow {
    u8 kind = 0;          // +0x00 (byte 0): 0 none, 2 by speed, 3 off the surfaces marked in 0x620288, others no term
    F lift_travel;        // +0x04 byte 1 x 0.001: the travel past which the lift is whole
    F lift_force;         // +0x08 byte 2 / 100: the lift, over the axle's divisor, x half the axle's load
    std::array<F, 2> lift_share; // +0x0C + 4 axle: bytes 3 / 4 / 100: each axle's divisor
    F term;               // +0x14 byte 5 x 0.001: the travel the part takes up (0x3F24A0)
    F speed_lo, speed_hi; // +0x18 / +0x1C bytes 6 / 7 km/h (kind 2)
};
struct SpeedDampTables {
    std::array<SpeedDampRow, 5> rows;
    std::array<u8, 24> fixed_aim{}; // 0x620288 + surface (SteerTables::fixed_aim)
};
constexpr u32 kSpeedDampRows = 0x622268, kSpeedDampBytes = 0x622240, kSpeedDampRowSize = 0x20;
SpeedDampTables speed_damp_tables(const Program& program);
template <class W, class B> SpeedDampTables speed_damp_tables(W&& read, B&& byte) {
    SpeedDampTables t;
    for (u32 r = 0; r < 5; ++r) {
        const u32 a = kSpeedDampRows + kSpeedDampRowSize * r;
        SpeedDampRow& row = t.rows[r];
        row.kind = byte(a), row.lift_travel = F::raw(read(a + 4)), row.lift_force = F::raw(read(a + 8));
        row.lift_share = {F::raw(read(a + 0xC)), F::raw(read(a + 0x10))};
        row.term = F::raw(read(a + 0x14)), row.speed_lo = F::raw(read(a + 0x18)), row.speed_hi = F::raw(read(a + 0x1C));
    }
    for (u32 s = 0; s < 24; ++s) t.fixed_aim[s] = byte(0x620288 + s);
    return t;
}
// The tables' values equal, bit for bit.
bool same(const SpeedDampTables& a, const SpeedDampTables& b);
// 0x3F24A0: row `row`'s term for wheel w: kind 2 its +0x14 shaded in from 0 at its lower speed to all at its upper (the
// speed along the body); kind 3 its +0x14 unless the wheel's surface is marked in 0x620288; else 0.
F speed_damp_term(const ex::Car& c, const SpeedDampTables& t, u8 row, int w);
// 0x3F2650: the force the part adds to wheel w's damper force at its travel x (0x369DE8, a loaded wheel, when P + 0x1076
// names a row): the progressive rate's part taken off, the spring rate x the row's term taken off, and with the travel
// past its static place by more than the term a lift: (the row's +0x08 / its axle's share, over the row's +0x04 x the
// travel within it) x the axle's load P + 0x1040 (SteerExtra::axle_load) x 0.5. 0 when the row's kind is 0.
F speed_damp(const ex::Car& c, const SpeedDampTables& t, u8 row, F axle_load, int w, F x);

} // namespace gt4r::sim::rules
