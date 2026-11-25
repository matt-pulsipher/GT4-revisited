#pragma once

// Gran Turismo 4 Prologue's phase 19 past its physics (docs/formats/prologue.md "The race step"): the speeds' bookkeeping
// and the routines of the car's feel, sound and view, on the engine's record as Prologue keeps it (rules_prologue.h).
// Addresses are SCPS-15055 v1.01's.

#include "gt4r/sim/rules_prologue.h"
#include "gt4r/sim/rules_feel.h"
#include "gt4r/sim/rules_speeds.h"

namespace gt4r::sim::prologue {

// What these routines keep beside the engine's record.
struct FeelPart {
    F speedo; // car + 0xC60: the speedometer (|speed| followed at 111.1 m/s a second)
};

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

} // namespace gt4r::sim::prologue
