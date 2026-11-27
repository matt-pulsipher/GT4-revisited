#pragma once

// Gran Turismo 4 Prologue's own routines of its race step that the shared engine's (Gran Turismo 3's) served before, each
// checked call by call against the program (tools/gt4diff's Prologue taps): phase 4's preparation, phases 20 .. 22 (docs/formats/
// prologue.md "Whose code each routine of the step is"). Addresses are SCPS-15055 v1.01's.

#include "gt4r/sim/rules_prologue.h"
#include "base/game_random.h"

namespace gt4r::sim::prologue {

struct FeelPart;

// The tables phase 4 reads, as the race's start makes them from the static block (0x503208):
struct StepTables {
    // 0x3DBA58 (into 0x504A78 .. 0x504A84): the slipstream's drag and downforce shares +0x01 / +0x00 and +0x02 / +0x00 (0 when
    // +0x00 is 0), how fast it comes on 10 / +0x05 (1 when 0) and goes 10 / +0x06 (0.25 when 0).
    ex::Slipstream slip;
    // 0x3B9530 (into 0x503E78, once: it sets the static byte +0x9C): the engine condition's limits from +0xA0 .. +0xA8: the
    // wear's -10,000 x +0xA0 (its grip loss +0xA1 x 0.001), 10,000 x +0xA2 and 10,000 x (+0xA2 + +0xA3) (+0xA4 x 0.001); the
    // oil's -10,000 x +0xA7 (+0xA6 x 0.001), 10,000 x (+0xA5 + +0xA7); 10,000 x +0xA8.
    ex::Condition condition;
    // The flaps' constants (0x50430C .. 0x504324, the image's; 0x3C26F0): the longitudinal acceleration's share, the hub
    // speed's kick and its chance, the rim speed's kick off a paved surface and its chance, the spring and the damping.
    F flap_accel, flap_speed, flap_chance, flap_rim, flap_rough, flap_spring, flap_damp;
};
StepTables step_tables(const Program& program);

// 0x3BEF38: each tire's grip by its wear (wheel +0x58) and the wear's level (+0x5D), over the wheel layout's wheels (the
// engine's 0x1E9E80 with the later builds' soft doubles and roundings): false (nothing done) with the wear off (its limit 0)
// or F +0x539 bit 4 set. At least the tire record's surface 11 grip.
bool wear_grip(ex::Car& c, const CarPart& x, const ex::WearParams& k);

// 0x3BF338: each wheel's peak force by the wheel layout: its load times the tire record's load curve at the load per width
// (0x3BF2C8), its wear grip (wear_grip on), the axle's grip (P +0xE08), the record's grip on the surface and the axle's
// camber curve (P +0x340) at the body's roll against the ground's (minus for a left wheel, plus for a right one, none in the
// middle; nothing for layout kind 1) plus the axle's camber (P +0xE14). Gran Turismo 4's 0x357418 but for the camber (its the
// wheel's) and without its computer driver's grip and the x1da kept.
void peak_force(ex::Car& c, const CarPart& x, bool wear);

// 0x3DBB70: the drag (against the speed) and each axle's downforce by the speed squared and the slipstream's share of them,
// the slipstream's state (F + 0x534) toward the pair's target (F + 0x530); then a device of kind 1 or 2 adds its own
// downforce to the rear (part_downforce, feel_prologue.h).
void aero(ex::Car& c, const CarPart& x, const FeelPart& f, const StepTables& t);

// 0x3B7AC8 with 0x3B9710: the full throttle torque, the torque curve at the engine's speed (0x3C15D8 = Gran Turismo 4's
// 0x359568) times the engine's condition factor (the wear's and the oil's, as the engine's 0x1DAD28); 0x3B7B30: the friction
// torque (P +0x1C8 x the engine's speed).
F condition_factor(const ex::Condition& k, F wear, F oil);

// 0x3BF608 (phase 4): the race's start hold (0x3B2140: F +0x4E0 while the conductor's count is above 0), the hold counter
// (F +0x53A) and the message timer (0x3C2DF0: F +0x521 toward 0, 0xFF kept) counted down, the step's length (0x3C9F00, Gran
// Turismo 4's 0x3658A0 arithmetic), the aerodynamics, the engine's torques (F +0x4B4 / +0x4B8), the peak forces (with the
// wear's grip by the race's rules, the conductor + 0x84) and the slip angles (0x3BF598 = Gran Turismo 4's 0x357690).
void prep(ex::Car& c, const CarPart& x, const FeelPart& f, const StepTables& t, s32 start_count, const ex::WearParams& wear);

// 0x3BBBB8 (phase 20): each tire's wear (wheel +0xA0) over the wheel layout, with the wear on (the race's rules' limit not 0):
// the wheels' wear bytes (+0x5C) cleared; none on a pit lane's triangle (F +0x539 bit 4); none on a kerb (surface 1) or a
// wheel off the ground; else the axle's load term (P +0xE98 + its gain P +0xEA0 x the load, at least 0) times the slip's
// intensity (wheel +0x30, at most the rules' cap) times the tire record's wear rate, a step's worth; the byte the intensity's
// share x 255. The engine's 0x1E8190 over the layout.
void tire_wear(ex::Car& c, const CarPart& x, const ex::WearParams& k);

// The conductor's part of the wear flags (phase 21): its virtual 43 (whether a car may go to the pits: 1 the single race's
// and the machine test's, 0 a license's, a training's and free run's) and virtual 39 (its mode: 3 a license's and a
// training's, else 0; 0x3C5DC0 gives 0 for a car a computer drives, F +0x3ED set).
struct PitRule {
    bool pits = true;
    s32 mode = 0;
};
// 0x3BBD80 (phase 21): with the conductor's pits and no pit request yet (F +0x539 bit 3): worn (a wheel of the layout whose
// tire's wear is not below 0 has a grip under the rules' "worn" factor, the wear on) or, in mode 4, a wheel of the layout
// with its byte +0x28 set: F +0x539 bit 2 set, and bit 3 too for control source 1 or 2. (The engine's 0x1E8340 tests the
// race flow's kind for control source 1; Prologue does not.)
void wear_flags(ex::Car& c, const CarPart& x, const ex::WearParams& k, const PitRule& r);

// 0x3C26F0 (phase 22): each flap of the layout's wheels (wheel +0x98, its rate +0x9C) as the engine's 0x1E6A38 on Prologue's
// constants, its draws on the VU0's random register (0x4482D8).
void flaps(ex::Car& c, const CarPart& x, const StepTables& t, gt3r::Vu0Random& rng);

// 0x375F58 (phase 19): each wheel of the layout's spring and damper (0x375848: the engine's 0x1E8E58 on the spring rate of
// 0x3C4478, Gran Turismo 4's 0x35D248; the special kinds 7 and 8's ride 0x3B15E8 with Prologue's table on, 0x503CB0), half
// track (0x375E90 = Gran Turismo 4's 0x36A690) and camber (0x375D78 = 0x36A578; the in-line layout's the command x the
// steering camber x cos(the roll against the ground's)); the anti-roll bars by wheel group (0x375A00 = 0x36A048); the in-line
// layout's lean term (F +0x3AC: the lateral acceleration x the mass x the body's up z); each wheel's body force: its spring,
// its group's bar by its side, its axle's downforce over the axle's wheels and the longitudinal transfer (P +0x38 x the force
// sum; x 2 / the axle's wheels when not 2) by its place, the lateral transfer (P +0x34 x -the lateral acceleration) by its
// side for a wheel with a partner. Gran Turismo 4's 0x36A758 without the wheels' normal speeds, contact states, the lateral
// geometry, the part that changes with speed and the wheel's own mass.
void suspension(ex::Car& c, CarPart& x, const Tables& t);

// 0x3C8878 (the car's set-up): new tires on the layout's wheels: the wear, its level and byte and the brake's heat 0, the
// grip 1; with the race's wear on, the wear the rules' fresh tire's (its level -0x3F below 0) and the grip 1 less its loss.
void tyres_start(ex::Car& c, const CarPart& x, const ex::WearParams& k);

} // namespace gt4r::sim::prologue
