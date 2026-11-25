#pragma once

// Gran Turismo 4 Prologue's phase 19 past its physics (docs/formats/prologue.md "The race step"): the speeds' bookkeeping
// and the routines of the car's feel, sound and view, on the engine's record as Prologue keeps it (rules_prologue.h).
// Addresses are SCPS-15055 v1.01's.

#include "gt4r/sim/rules_prologue.h"
#include "gt4r/sim/rules_effects.h"
#include "gt4r/sim/rules_feel.h"
#include "gt4r/sim/rules_force.h"
#include "gt4r/sim/rules_near.h"
#include "gt4r/sim/rules_phases.h"
#include "gt4r/sim/rules_speeds.h"

namespace gt4r::sim::prologue {

// The steering wheel's force in the car's pad block (kPadBlock): 0x3BC7B0 clears it at the car's set-up.
struct ForcePart {
    u8 out = 0x80;              // +0x02: 0x80 - the force (within +-1) x 127, truncated
    u8 count = 0;               // +0x03: the smoothing's counter (kind 3)
    F last;                     // +0x04: the last call's force before the aligning term (kinds 1 .. 3)
    std::array<u8, 3> wheel{};  // +0x0D .. +0x0F: rules::ForceMode::wheel for a kind 6 wheel on a paved course, else 0
};

// What these routines keep beside the engine's record.
struct FeelPart {
    // The aerodynamic device's states: F + 0x514 how far it is out (0 .. 1), F + 0x518 its second (-1 .. 1), F + 0x51C /
    // + 0x51D their flags (rules::NearExtra's, rules_near.h).
    F part_state;
    rules::NearExtra part;
    ForcePart force;
    F speedo; // car + 0xC60: the speedometer (|speed| followed at 111.1 m/s a second)
    F start_pace;    // a rolling start's speed while the car's driver brings it to the line (m/s; 0: none)
    u16 penalty = 0; // F + 0x51E / 0x51F (low, high): the steps left of the speed cap a hit gave
};

// 0x3B1BD0 (the car's set-up): the device's states as the car starts: kind 1 out at or past the upper speed, kind 2 by the
// speed, the others in.
void part_start(const ex::Car& c, const CarPart& x, FeelPart& f);
// 0x3B1C58 (phase 15): the device moves: Gran Turismo 4's 0x3F28B8 (rules::speed_part) with kinds 1, 2 and 4 only.
void part_step(ex::Car& c, const CarPart& x, FeelPart& f);
// 0x3DBB70's last part (phase 4, after the engine's aerodynamics): a device of kind 1 or 2 adds its downforce to the rear by
// how far it is out.
void part_downforce(ex::Car& c, const CarPart& x, const FeelPart& f, const ex::Slipstream& s);

// 0x3C6078 (the race's judge of a hit, DynamicsConductor's virtual 45; Gran Turismo 4's 0x35FCC8 without its mark): whether a
// hit gives the car a penalty: control source 0, its race not ended, the rules' +0x4A set and none running; a hit at the front
// (rules::part_class < 3): a wall's (`kind` 0) change of speed (the impulse over the mass) at or past +0x49, a car's (1)
// strength x 100 at or past +0x48.
bool hit_penalty(const ex::Car& c, const rules::HitRules& r, int kind, int part, F strength, bool running);

// The speeds' tables of a car (Gran Turismo 4's rules::SpeedTables in Prologue's places): the two-wheeler's lean and the
// roll rate's switches (0x5042B8 / 0x5042B4, 0 in the image), the special row's (0x5040F8 + 0x78 row, by the parameter
// block's +0x33; 0x3C1F60) +0x30 / +0x34 and +0x10.
rules::SpeedTables speed_tables(const Program& program, u8 special_row);

// 0x3C5740 (phase 19): the speeds along and across the ground's frame and each hub's velocity by the wheel layout (0x3C5310
// with 0x3C50A0: Gran Turismo 4's 0x35D920 / 0x35D6B0); the odometer and the distances (0x3C5698: Gran Turismo 3's, on the
// signed speed); the engine's condition (0x3B1860 / 0x3B1958 on the block car + 0x4D4: its wear and oil, by the step's
// distance |speed| x dt (0x3B9700) at every step, at most the condition table's limits (0x503E78, made from the static block
// by 0x3B9530: the engine's Condition)); the top speed while the car runs (F + 0x4E4 0); the speedometer at car + 0xC60
// (0x3C55F8: 111.1 m/s a second, 0x42DE38E2); the free wheels (0x3C5380: Gran Turismo 4's 0x35D990).
void speeds(ex::Car& c, FeelPart& x, const rules::WheelLayout& l, const rules::SpeedTables& t, const ex::Condition& k);

// The view's follower's block of car k in the conductor: cond + 0xDB48 + 0x14 k (Gran Turismo 4's ViewState's first five
// words; k the record's F + 0x3EC).
constexpr u32 kViewBlock = 0xDB48, kViewSize = 0x14;

// The tables of Prologue's feel (Gran Turismo 4's rules::FeelTables in Prologue's places): the view's heading gain and
// damping (0x571C98 / 0x571CA0: 30, 10); the engine sound's level scales (Gran Turismo 4's cue words 25 .. 28: Prologue's
// 0x7148A0 + 0x64 .. + 0x70, made at the race's start by 0x3B8720 from the effects block 0x504B08 + 0x1E0 + 0x1C .. + 0x1F:
// / 100, / 100, / 100, / 10, the engine's RaceInputs::sound_scales on Prologue's block).
rules::FeelTables feel_tables(const Program& program, const std::array<F, 4>& sound_scales);

// 0x3B4238 (phase 19): the view's follower, Gran Turismo 4's 0x345658 (rules::view_follow) with Prologue's gains.
inline void view_follow(ex::Car& c, rules::ViewState& v, const rules::FeelTables& t) { rules::view_follow(c, v, t); }

// 0x3B9020 (phase 19): the engine sound's levels, Gran Turismo 4's 0x350080 (rules::engine_sound) for every car (Prologue
// has no draw mode's test); the gearbox's kind the parameter block's +0x20A.
inline void engine_sound(ex::Car& c, const rules::FeelTables& t) { rules::engine_sound(c, t, c.p.d.kind, 3); }

// The tire effects' tables: Gran Turismo 4's rules::SlideTables as Prologue's race start builds them (0x3CAE00 with the
// fix-ups 0x3E1E10, Gran Turismo 4's 0x366D68 and 0x3FAA10) from its effects block 0x504B08 (0x210 bytes, the engine's
// Effects table): into 0x714B60 / 0x714D70 / 0x714F80, the loose ground's measures 0x715190.
constexpr u32 kEffectsBlock = 0x504B08;
rules::SlideTables slide_tables(const Program& program);

// The kind of surface (0 .. 2) of Prologue's 16 surfaces by its index (the jump table 0x571E10 on index - 2): 2 .. 5
// kind 1; 6 and 12 .. 14 kind 2; the others 0.
int slide_kind(u8 surface);

// 0x3CB1E0 (phase 19): each wheel's sliding measures (the slip intensity, the skid's pitch, the smoke) and the car's dirt:
// Gran Turismo 4's 0x367148 (rules::slide_effects) on Prologue's surfaces: the load and rim speed's measures on surfaces 1
// and 13 (Gran Turismo 4: 1, 10, 11, 21), its own kinds (slide_kind; no surface 12 as kind 1 for the smoke), the dirt the
// smoke of the wheels on surfaces of kind 1 or 2 x 8 on a course of kind 0 (the conductor's byte + 0xDBED, 0xFF as 0;
// 0x3B63B0) x 0.001 x dt, into the condition block's +0xC at most 0.8 (0x3B1B00) whatever the engine's state.
void tire_effects(ex::Car& c, const rules::WheelLayout& l, const rules::SlideTables& t, u8 course_kind);

// The pad's vibration block of car k in the conductor: cond + 0xDAC8 + 0x10 k (k the record's F + 0x4C4; Gran Turismo 4's
// cond + 0xCA58 + 0x10 k), +0x01 the level.
constexpr u32 kPadBlock = 0xDAC8, kPadSize = 0x10;

// 0x3CE1A8 (phase 19): the pad's vibration, Gran Turismo 4's 0x36CEE8 (rules::pad_rumble) but a wall's hit counted from
// above 0 (Gran Turismo 4: 0.01) and the kerb strips Prologue's surfaces 1 and 13.
void pad_rumble(const ex::Car& c, const rules::WheelLayout& l, std::array<u8, 2>& pad);

// The steering force's tables (0x714958 + 0x8C m, made at the race's start by 0x3BC350): Gran Turismo 4's modes
// (rules::ForceMode, its 0x353938's arithmetic) without the steering change's factor, from the rows 0x5049C4 + 0x20 m of the
// controller settings and the bytes 0x503F20 .. 0x503F26; the kind 6 ratios are Controls::jog_scale. `smooth`: the row's
// +0x04, the smoothing's kind.
struct ForceTables {
    std::array<rules::ForceMode, 3> mode;
    std::array<u8, 3> smooth{};
};
ForceTables force_tables(const Program& program, const Controls& k);

// 0x3BC7F0 (phase 19, after the pad's vibration): the steering wheel's force. Gran Turismo 4's 0x353EE8 (rules::steer_force)
// but: a computer's car without the kind 6 doubling; the mode's second row by the parameter block's +0x33; a loose course is
// kind 2 only; the steering offset's term with the steering mode on at every speed (then no slow-speed term); no factor of
// the steering's change, no rise after the start; and the sum before the aligning term smoothed by `smooth` (0 none, 1 the
// mean with the last, 2 the lesser of it and the last, 3 Gran Turismo 3's peak hold; another: 0). `strength`: the controller's
// answer to its virtual +0x20 (RaceInput::vf4 0x3FC188: 0 unless its port gives one).
void steer_force(const ex::Car& c, ForcePart& o, const CarPart& x, const ForceTables& t, const rules::SteerTables& g, u8 course_kind,
                 int strength = 0);

} // namespace gt4r::sim::prologue
