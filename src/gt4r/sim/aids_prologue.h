#pragma once

// Gran Turismo 4 Prologue's driving aids, phase 9 of its step (0x3E0838; docs/formats/prologue.md "The race step"): the
// steering indicators, the traction control, the brakes (the dirt cut, the stability control, the ABS) and the race's speed
// caps. Its routines are Gran Turismo 4's (rules_aids.h: the brakes' shares 0x3E1008 = 0x3F92D0, their application
// 0x3E1078 = 0x3F9340) with its own stability control (0x3E11F8: Gran Turismo 4's 0x3F94C0 without the throttle cut, the
// multiplier's operands in another order), traction control (0x3E1960) and brakes (0x3E1540): the curve A value they read is
// the wheel's byte +0x27 (x 1 / 128) that the tire slip force (0x3BB138) leaves, and the switches are its static block's and
// its words 0x504B02 .. 0x504B04. Each equal to the program's at every call on the test drive (gt4r_diff's taps).

#include "gt4r/sim/rules_aids.h"
#include "gt4r/sim/rules_prologue.h"
#include "sim/exact/tick.h"

namespace gt4r::sim::prologue {

// The aids' switches and constants, from the program.
struct AidTables {
    std::array<u8, 2> cap_blend{}; // static +0xE1 + axle: the ABS target's blend toward curve D's falling side (percent)
    u8 tcs_mode = 0;               // static +0xE3: 1 the TCS target from curve D's rising side, without the lateral fade
    u8 dirt_steer = 0;             // static +0xB5: the dirt cut's full steering (degrees; 0: no cut)
    std::array<u8, 2> dirt_cut{};  // static +0xB6 / +0xB7: the front's and rear's brake cut at full steering (percent)
    u8 c_status = 0;               // 0x504B02 the stability control's channels C / E count as acting and as on
    u8 c_throttle = 0;             // 0x504B03 channel C acts with the throttle open too
    u8 e_quiet = 0;                // 0x504B04 channel E neither counts as acting nor as on
    std::array<u8, 2> over_angle{}; // static +0xAD / +0xAE: the oversteer's rear slip angle at rest and at full speed (degrees)
    std::array<u8, 2> over_gain{};  // static +0xAF / +0xB0: its gain at rest and at full speed (x 0.1)
};
AidTables aid_tables(const Program& program);

// What the phase reads of the race: the course is dirt (0x3B63E0: the conductor's course kind 2), the rule block's speed
// caps (+0x42 a car's, +0x43 a computer's (control source 2), +0x4B while the car's penalty (`penalty`: its counter F + 0x51E runs);
// km/h, 0 none).
struct AidRace {
    bool dirt = false;
    std::array<u8, 2> speed_cap{};
    u8 penalty_cap = 0;
    bool penalty = false;
};

// The wheel's curve A byte (+0x27, 0x3BB138: the curve's value x 128) as its readers take it: x 1 / 128.
F curve_a_byte(F curve_a);

// 0x3E11A8: the stability control's switches on: channel A or D; with 0x504B02 also C, or E without 0x504B04.
u8 ascc_on(const AidTables& g, const ex::Aids& a);
// 0x3E11F8: the axles' ABS targets scaled (P +0xE58), then with `enabled` the stability control's channels by the brake pedal
// (Gran Turismo 4's 0x3F94C0 without the throttle cut, in Prologue's order of operands): off it A (the oversteer) adds brake
// to the outer wheels and C (the understeer, with the throttle off unless 0x504B03) to the inner ones; on it D and E ease
// the inner and outer wheels' targets and pressures by 1 - (indicator - 0.1) x gain. The status (F +0x524): 2 acting, else 1
// when on.
void ascc(ex::Car& c, const AidTables& g, const ex::Aids& a, rules::BrakeShares& s, bool enabled);
// 0x3E1960: the traction control on block `a`.
void tcs(ex::Car& c, const CarPart& x, const AidTables& g, const ex::Aids& a);
// 0x3E1540: the brakes on block `a` (`ascc_enabled`: the stability control acts).
void brakes(ex::Car& c, const CarPart& x, const AidTables& g, const AidRace& r, const ex::Aids& a, bool ascc_enabled);
// 0x3E0670: the throttle at most what holds `target` (m/s) in the car's gear.
void hold_speed(ex::Car& c, F target);
// 0x3E02A0: the understeer and oversteer indicators (F +0x39C / +0x3A0), 0 at a standstill or going back. The grip the sum of
// the layout's wheels' tire records' grips. Understeer: turning the way it steers with the lateral acceleration past 0.2 of
// the grip, the yaw rate (and half its acceleration while that adds to it) short of speed x sin(the command) / the wheelbase.
// Oversteer, a car a computer drives (control source 1 or 2, or a driver kind): from 25 km/h, the yaw rate times the speed
// beyond the lateral acceleration while they turn opposite ways, x 2 / the grip; else the greatest slip angle of the wheels
// behind the center of gravity past the static block's angle at the speed's share (x 0.018 a m/s, at most 1), times its gain
// over what is left to 90 degrees, at most 1. The engine's 0x1E3708 over the layout, with the 25 km/h always.
void steer_indicators(ex::Car& c, const CarPart& x, const AidTables& g);
// 0x3E0838: the phase.
void aids(ex::Car& c, const CarPart& x, const AidTables& g, const AidRace& r, const ex::AidInputs& in);

} // namespace gt4r::sim::prologue
