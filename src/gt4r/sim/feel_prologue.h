#pragma once

// Gran Turismo 4 Prologue's phase 19 past its physics (docs/formats/prologue.md "The race step"): the speeds' bookkeeping
// and the routines of the car's feel, sound and view, on the engine's record as Prologue keeps it (rules_prologue.h).
// Addresses are SCPS-15055 v1.01's.

#include "gt4r/sim/rules_prologue.h"
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

} // namespace gt4r::sim::prologue
