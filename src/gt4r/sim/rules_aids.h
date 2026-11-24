#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the driving aids, phase 9 of the step (0x3F79A0):
// the steering indicators, TCS, the brakes (the dirt cut, ASCC, ABS), the race's speed caps and the
// cornering throttle hold.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_drive.h"
#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim::rules {

// An aid block (0x34 bytes) is the engine's ex::Aids, the same layout: +0x00 channel A's gain, +0x04 its
// threshold, +0x08 channel D's gain, +0x0C / +0x0E A's and D's wheels (front, rear), +0x10 C's gain, +0x14 its
// threshold, +0x18 E's gain, +0x1C / +0x1E C's and E's wheels, +0x20 the TCS's gain (0: off), +0x24 / +0x28 its
// lateral fade's slope and start, +0x2C its target's scale, +0x30 .. +0x33 channels A, D, C, E on. A car's own
// is P + 0x1098; a computer driver's is its parameter block + 0x24 (0x463BE0).

// What these routines keep that the engine's record has no member for.
struct AidExtra {
    ex::Aids own;                    // P + 0x1098 the car's own aid block
    std::array<F, 2> abs_gain;       // P + 0x1024 per axle: the ABS's slope past the slip target
    std::array<F, 2> slip_scale;     // P + 0x102C per axle: the braking slip target's scale
    std::array<F, 2> abs_peak;       // P + 0x1034 per axle: the peak force's scale for the ABS
    u8 mark_mode = 0;                // P + 0x32 (2: each wheel's ASCC mark is kept)
    F understeer, understeer_v;      // F + 0x470 / + 0x474 the understeer indicator and its follower's rate
    F oversteer, oversteer_v;        // F + 0x478 / + 0x47C the oversteer indicator and its follower's rate
    F road_load;                     // F + 0x5BC the resistances' torque term (phase 4: -(rolling + drag - weight x slope))
    F fuel_capacity, fuel;           // F + 0x4CC / + 0x4D0 (liters; 0x354AC0 adds the fuel x 0.74 to the mass)
    F tcs_throttle, tcs_throttle_v;  // F + 0x644 / + 0x648 the TCS's throttle follower above 100 km/h and its rate
    u16 start_count = 0;             // F + 0x60E steps of the start's speed cap left (0x35FC48: 60 x seconds)
    s8 pit_limit = 0;                // F + 0x5C3 not 0: the 10 km/h cap (set by 0x35B060)
    u8 assist = 0;                   // F + 0x4E5 bit 1: the cornering throttle hold ❓ who sets it
    u8 ascc_status = 0;              // F + 0x5F8 0 off, 1 on, 2 acting
    u8 tcs_status = 0;               // F + 0x5F9 0 off, 1 on, 2 limiting
    s8 scale_on = 0;                 // car + 0x55 not 0: the indicators' scales (0x6202A4 / A8) may apply ❓
    u8 tcs_class = 0;                // car + 0x81 the computer driver's TCS class (0x6220E0's row)
    std::array<u8, 4> ascc_mark{};   // wheel + 0x88 1: ASCC changed the wheel's brake this step (with mark_mode 2)
};
// Every member of AidExtra with its place in Gran Turismo 4's record: v(name, index (-1: single), block, offset, member).
template <class X, class V> void aids_fields(X& x, V&& v) {
    using layout::Block;
    const Block P = Block::Params, C = Block::Car;
    const u32 A = 0x1098;
    auto& a = x.own;
    v("aids.a_gain", -1, P, A + 0x00, a.a_gain), v("aids.a_thr", -1, P, A + 0x04, a.a_thr), v("aids.d_gain", -1, P, A + 0x08, a.d_gain);
    v("aids.c_gain", -1, P, A + 0x10, a.c_gain), v("aids.c_thr", -1, P, A + 0x14, a.c_thr), v("aids.e_gain", -1, P, A + 0x18, a.e_gain);
    v("aids.tcs_gain", -1, P, A + 0x20, a.tcs_gain), v("aids.tcs_lat_a", -1, P, A + 0x24, a.tcs_lat_a);
    v("aids.tcs_lat_b", -1, P, A + 0x28, a.tcs_lat_b), v("aids.tcs_scale", -1, P, A + 0x2C, a.tcs_scale);
    for (int k = 0; k < 2; ++k) {
        const u32 K = u32(k);
        v("aids.a_wheels", k, P, A + 0x0C + K, a.a_wheels[K]), v("aids.d_wheels", k, P, A + 0x0E + K, a.d_wheels[K]);
        v("aids.c_wheels", k, P, A + 0x1C + K, a.c_wheels[K]), v("aids.e_wheels", k, P, A + 0x1E + K, a.e_wheels[K]);
        v("aids.abs_gain", k, P, 0x1024 + 4 * K, x.abs_gain[K]), v("aids.slip_scale", k, P, 0x102C + 4 * K, x.slip_scale[K]);
        v("aids.abs_peak", k, P, 0x1034 + 4 * K, x.abs_peak[K]);
    }
    for (int k = 0; k < 4; ++k) v("aids.on", k, P, A + 0x30 + u32(k), a.on[size_t(k)]);
    v("aids.mark_mode", -1, P, 0x32, x.mark_mode);
    const u32 S = layout::kState;
    v("aids.understeer", -1, C, S + 0x470, x.understeer), v("aids.understeer_v", -1, C, S + 0x474, x.understeer_v);
    v("aids.oversteer", -1, C, S + 0x478, x.oversteer), v("aids.oversteer_v", -1, C, S + 0x47C, x.oversteer_v);
    v("aids.road_load", -1, C, S + 0x5BC, x.road_load);
    v("aids.fuel_capacity", -1, C, S + 0x4CC, x.fuel_capacity), v("aids.fuel", -1, C, S + 0x4D0, x.fuel);
    v("aids.tcs_throttle", -1, C, S + 0x644, x.tcs_throttle), v("aids.tcs_throttle_v", -1, C, S + 0x648, x.tcs_throttle_v);
    v("aids.start_count", -1, C, S + 0x60E, x.start_count), v("aids.pit_limit", -1, C, S + 0x5C3, x.pit_limit);
    v("aids.assist", -1, C, S + 0x4E5, x.assist);
    v("aids.ascc_status", -1, C, S + 0x5F8, x.ascc_status), v("aids.tcs_status", -1, C, S + 0x5F9, x.tcs_status);
    v("aids.scale_on", -1, C, 0x55, x.scale_on), v("aids.tcs_class", -1, C, 0x81, x.tcs_class);
    for (int w = 0; w < 4; ++w) v("w.ascc_mark", w, C, layout::kWheel0 + layout::kWheelSize * u32(w) + 0x88, x.ascc_mark[size_t(w)]);
}

// The program's switches and constants these routines read: from the static physics block (0x620CD8 + offset)
// and the words and bytes past it.
struct AidTables {
    std::array<u8, 2> over_angle{}; // +0xDD / +0xDE the oversteer indicator's slip angle threshold (degrees) at rest / at speed
    std::array<u8, 2> over_gain{};  // +0xDF / +0xE0 its gain (x 0.1) at rest / at speed
    u8 dirt_steer = 0;              // +0xE5 the dirt cut's full steering (degrees; 0: no cut)
    std::array<u8, 2> dirt_cut{};   // +0xE6 / +0xE7 the front's and rear's brake cut at full steering (percent)
    std::array<u8, 2> cap_blend{};  // +0x115 + axle: the ABS target's blend toward curve D's falling side (percent)
    u8 tcs_mode = 0;                // +0x117 1: the TCS target from curve D's rising side, without the lateral fade
    u8 under_scale_on = 0;          // +0x134 the understeer indicator scaled by under_scale (with the car's scale_on)
    u8 over_scale_on = 0;           // +0x135 the oversteer indicator by over_scale
    u8 under_follow = 0;            // +0x136 the understeer indicator through a follower
    u8 over_follow = 0;             // +0x137 the oversteer indicator through a follower
    u8 ascc_cut = 0;                // +0x138 ASCC's channels C / E cut a player's throttle by the understeer
    u8 tcs_speed_on = 0;            // +0x146 the TCS target raised with speed
    u8 tcs_speed_lo = 0, tcs_speed_hi = 0; // +0x147 / +0x148 from and to these speeds (x 10 km/h)
    u8 tcs_speed_gain = 0;          // +0x149 by up to 1 + this x 0.01
    F ascc_cut_gain;                // 0x6202A0 the throttle cut per unit of understeer
    F under_scale, over_scale;      // 0x6202A4 / 0x6202A8
    u8 c_status = 0;                // 0x6224A6 channels C / E count as acting and as on
    u8 c_throttle = 0;              // 0x6224A7 channel C acts on the throttle too
    u8 e_quiet = 0;                 // 0x6224A8 channel E neither counts as acting nor as on
    std::array<F, 5> tcs_class;     // 0x6220E0 the TCS target's factor by a computer driver's class: (s8 at 0x6220A0 + k,
                                    // + 100) / 100, made by 0x3F1790 at the race's configuration
};
constexpr u32 kAidStatic = 0x620CD8, kAscc = 0x6202A0, kAsccSwitches = 0x6224A6, kTcsClass = 0x6220E0, kTcsClassBytes = 0x6220A0;
AidTables aid_tables(const Program& program);
// From the words and bytes at those addresses (`read(address)` a word, `byte(address)` a byte); the TCS classes'
// factors as the race's configuration left them at 0x6220E0.
template <class W, class B> AidTables aid_tables(W&& read, B&& byte) {
    AidTables g;
    const u32 S = kAidStatic;
    for (u32 k = 0; k < 2; ++k) {
        g.over_angle[k] = byte(S + 0xDD + k), g.over_gain[k] = byte(S + 0xDF + k);
        g.dirt_cut[k] = byte(S + 0xE6 + k), g.cap_blend[k] = byte(S + 0x115 + k);
    }
    g.dirt_steer = byte(S + 0xE5), g.tcs_mode = byte(S + 0x117);
    g.under_scale_on = byte(S + 0x134), g.over_scale_on = byte(S + 0x135), g.under_follow = byte(S + 0x136), g.over_follow = byte(S + 0x137);
    g.ascc_cut = byte(S + 0x138);
    g.tcs_speed_on = byte(S + 0x146), g.tcs_speed_lo = byte(S + 0x147), g.tcs_speed_hi = byte(S + 0x148), g.tcs_speed_gain = byte(S + 0x149);
    g.ascc_cut_gain = F::raw(read(kAscc)), g.under_scale = F::raw(read(kAscc + 4)), g.over_scale = F::raw(read(kAscc + 8));
    g.c_status = byte(kAsccSwitches), g.c_throttle = byte(kAsccSwitches + 1), g.e_quiet = byte(kAsccSwitches + 2);
    for (u32 k = 0; k < 5; ++k) g.tcs_class[k] = F::raw(read(kTcsClass + 4 * k));
    return g;
}
// 0x3F1790's factors from the signed bytes at 0x6220A0.
std::array<F, 5> tcs_classes(const s8* bytes);

// What the phase reads of the race (the conductor).
struct AidRace {
    u8 course_kind = 0;               // 0x34C998: the conductor's course kind (cond + 0x10000 - 0x3423, 0xFF as 0) 2 → 1, 3 → 2, else 0
    std::array<u8, 2> speed_cap{};    // the rule block (cond + 0xF818) +0x42: a car's cap (km/h; 0 none) unless its control
                                      // source is 2, +0x43 then
    u8 start_cap = 0;                 // +0x4B the cap while the start counter (F + 0x60E) runs
    u8 fuel_cap = 0;                  // +0x4C the cap out of fuel
    u8 start_speed = 0;               // 0x3F7880's speed (km/h): the rule block's +0x6E, else (the conductor's virtual +0x30
                                      // 0, as the single-player conductor's) the car's own byte at cond + 0x10000 - 0x79A + index,
                                      // else the rule block's +0x4E
    F lap_length;                     // 0x34C230: the course's lap (its road +0x14)
};

// 0x3F9CD0: 1 when the block's TCS gain is not 0.
u8 block_tcs_on(const ex::Aids& a);
// 0x3F9470: 1 when channel A or D is on; else, with the switch 0x6224A6, when C is, or E is without 0x6224A8.
u8 block_ascc_on(const ex::Aids& a, const AidTables& g);

// A tire record's index for wheel w on a surface (0x360F80: its axle's index byte for the surface).
size_t tire_index(const TireExtra& t, const WheelLayout& l, int w, u8 surface);
// A tire record's curve D table (+0x170: curve C's points and axis, curve D's values; ✔ on every car checked).
// 0x3596C0: on curve D's table (x: curve C's axis, y: curve D; n points) the slip where the curve rises through
// v, searched up from the first point: x[0] at or below y[0]; 0 once x is not negative; x[n - 1] past the last.
F rising_slip(s16 n, const F* x, const F* y, F v);
// 0x359778: the slip where curve D falls through v, searched down from the last point: x[n - 1] at or below
// y[n - 1]; 0 once x is not positive; x[0] past the first.
F falling_slip(s16 n, const F* x, const F* y, F v);

// 0x3F71E8: the steering indicators (F + 0x470 / 0x478; both 0 at or below standstill). Understeer: the yaw rate
// (plus half the yaw acceleration when they agree) short of v sin(steer) / wheelbase, as a share of it, while the
// lateral acceleration is over 0.2 of the tires' grip; scaled for a player's car with scale_on. Oversteer, a
// player's car: the largest slip angle of the wheels behind the center of gravity past a threshold that blends
// with speed (x 0.018, at most 1) from over_angle[0] to [1], times the gain blended the same way over the rest
// to 90 degrees, at most 1 (below the threshold the slip angle itself stays the indicator); a car a computer
// drives (control source 1 / 2 or a driver kind): from 25 km/h, the yaw rate x speed beyond the lateral
// acceleration when they turn opposite ways, twice over the grip. Each through a follower when its switch is
// on: rate += ((target - value) x 500 - rate x 20) x dt, value += rate x dt.
void indicators(ex::Car& c, AidExtra& x, const TireExtra& t, const WheelLayout& l, const AidTables& g);
// 0x3F76E8: the throttle that holds `target` (m/s) in the car's gear: the resistances' term and the friction torque
// over the engine's full torque (both through the gear's speed factor), less the speed past target + 0.5 km/h per
// 5 km/h; 0 .. 1 (1 without torque).
F hold_throttle(const ex::Car& c, const AidExtra& x, const DriveExtra& d, F target);
// 0x3F7880: a rolling start's pace for a car of driver kind 1 before its first lap: 1 (with `speed`, km/h) while
// it is more than 3 seconds at that speed short of the line, 2 (`speed` 0) after, 0 otherwise.
int start_pace(const ex::Car& c, const AidRace& r, u32& speed);

// The brakes' shares the ASCC sets and 0x3F9340 applies (0x3F92D0 starts them: 1, 1, 1, 1, 0).
struct BrakeShares {
    std::array<F, 2> axle_brake;      // +0x00 per axle: the brake pressure's factor (the dirt cut)
    std::array<F, 2> axle_cap;        // +0x08 per axle: the ABS target's factor (the axle's slip_scale)
    std::array<F, 4> cap_mul;         // +0x10 per wheel slot (axle x 2 + side)
    std::array<F, 4> brake_mul;       // +0x20
    std::array<F, 4> brake_add;       // +0x30
};
BrakeShares brake_shares();
// 0x3F94C0: the axles' ABS targets scaled; with `enabled`, ASCC's channels by the brake pedal: off it A (the
// oversteer past its threshold) adds brake to the outer wheels and C (the understeer, with the throttle off unless
// 0x6224A7) to the inner ones; on it D (oversteer) and E (understeer) ease the inner and outer wheels' targets and
// pressures, 1 - (indicator - 0.1) x gain. Channels C / E acting cut a player's throttle by ascc_cut_gain x the
// understeer (with the switch +0x138). The status (F + 0x5F8): 2 acting, else 1 when on (0x3F9470).
void ascc(ex::Car& c, AidExtra& x, const ex::Aids& a, BrakeShares& s, bool enabled, const AidTables& g);
// 0x3F9340: each wheel's brake pressure and ABS target by its axle's and slot's shares (the pressure at most 1).
void apply_shares(ex::Car& c, const WheelLayout& l, const BrakeShares& s, std::array<F, 4>& cap);
// 0x3F9870: the brakes: each wheel's ABS target (its tire record's braking slip, blended toward curve D's falling
// side at the wheel's curve A value where the record's direct slip byte is 1), the dirt cut on a loose course by
// the steering, ASCC (`ascc_enabled`), the ASCC marks (P + 0x32 = 2), the shares, then the ABS: past the target
// the pressure falls with slope abs_gain x slip gain, x the peak force's share; a pressure that comes to 0 is
// kept at 0.001.
void brakes(ex::Car& c, AidExtra& x, const TireExtra& t, const WheelLayout& l, const AidTables& g, const AidRace& r, const ex::Aids& a,
            bool ascc_enabled);
// 0x3F9D00: TCS: the status (F + 0x5F9) 1 when on; the most negative slip ratio of the loaded driven wheels (by the
// layout: 0 and 5 .. 7 the wheels behind the center of gravity, 1 those ahead, others all) against its tire's
// driving slip target (with tcs_mode 1 at least curve D's rising side at curve A's value; else x the lateral fade),
// x the block's scale, x the class factor for control source 1 / driver kind 0, raised with speed (tcs_speed_*);
// the throttle x (1 + (slip - target) x gain x slip gain), 0 .. 1, directly below 100 km/h, through a follower
// (x 1.1, 500 / 50 at 1/60 s, at most the throttle) at and above it; status 2 when it limits.
void tcs(ex::Car& c, AidExtra& x, const TireExtra& t, const DriveExtra& d, const WheelLayout& l, const AidTables& g, const ex::Aids& a);
// 0x3F77B8: the car's own aids: TCS and the brakes with ASCC while the handbrake is under 0.1.
void own_aids(ex::Car& c, AidExtra& x, const TireExtra& t, const DriveExtra& d, const WheelLayout& l, const AidTables& g, const AidRace& r);
// 0x3F7828: a computer driver's aids (its block `a`): TCS and the brakes with ASCC unless the clutch state is 2.
void driver_aids(ex::Car& c, AidExtra& x, const TireExtra& t, const DriveExtra& d, const WheelLayout& l, const AidTables& g, const AidRace& r,
                 const ex::Aids& a);

// 0x3F79A0, the phase: the indicators; both statuses 0; by the control source: 0 (a pad) the car's own aids with
// driver kind 0 or the hold counter running, else the driver's with the car's own block's switches as the
// statuses; 1 / 2 the driver's (statuses 0); others none. Then the speed cap: the rule block's for the control
// source, the start's while its counter runs and out of fuel (the lower of the set ones), 10 km/h with the pit
// limit, the rolling start's pace; the throttle at most what holds it (0x3F76E8). Then with F + 0x4E5 bit 1, for
// control source 0 / 3 and driver kind 0: the throttle at least what holds the speed x (lat_g^2 x 1.5 - 0.15,
// 0 .. 1). `driver`: the computer driver's aid block (null: none, a block of zeros). The engine's understeer,
// oversteer, tcs_status and ascc_status follow the phase's (for its readers; they have no place of their own).
void aids(ex::Car& c, AidExtra& x, const TireExtra& t, const DriveExtra& d, const WheelLayout& l, const AidTables& g, const AidRace& r,
          const ex::Aids* driver);

// An aid block's bytes: channel A's gain and threshold, D's gain, the channels' wheels (bits 0 .. 7: A, D, C, E,
// front then rear), C's gain and threshold, E's gain (all x 0.01), the TCS's lateral slope (x 0.1), lateral start
// (x 0.01), gain (x 0.1) and scale (x 0.01; 0 or 100: 1). A channel is on with a wheel and a gain.
struct AidBytes {
    u8 a_gain = 0, a_thr = 0, d_gain = 0, wheels = 0, c_gain = 0, c_thr = 0, e_gain = 0;
    u8 tcs_lat_a = 0, tcs_lat_b = 0, tcs_gain = 0, tcs_scale = 0;
};
ex::Aids aid_block(const AidBytes& b);
// 0x466C20's aid block of a computer driver (its parameter block + 0x24) from the course set's bytes `s`: s9, s10,
// s20, s19, s21 .. s23, s11, s12, s13, s24.
ex::Aids driver_block(const u8* s);
// The converter's (0x361908) part of AidExtra from the spec's bytes: the car's own block (P + 0x1098) from spec
// +0x1BA, +0x1BB, +0x1BC, +0x1B8, +0x1BD .. +0x1BF, +0x1C0 .. +0x1C3; per axle k the ABS gain (P + 0x1024) spec
// +0x1A8 + k x 0.1, the braking slip target's scale (P + 0x102C) spec +0x1AA + k x 0.01, the peak force's scale
// (P + 0x1034) the tire's radius (P + 0x2C4) over the brake torque (P + 0x1010; 0 when the spec's brake byte
// +0x178 + k is 0); P + 0x32 the spec's byte +0x01.
void aid_extras(const u8* spec, const std::array<F, 2>& brake_torque, const std::array<F, 2>& tire_radius, AidExtra& x);

} // namespace gt4r::sim::rules
