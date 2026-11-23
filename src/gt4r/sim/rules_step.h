#pragma once

// Gran Turismo 4's own routines (rules_*.h) put into the shared engine's phases for a session's step:
// what is ported runs in place of the engine's, the rest is the engine's (docs/specs/physics.md "What
// runs where").

#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_chassis.h"
#include "gt4r/sim/rules_tires.h"
#include "gt4r/sim/rules_wear.h"
#include "sim/exact/race_inputs.h"

namespace gt4r::sim::rules {

// The race's tables Gran Turismo 4's routines read.
struct Tables {
    SlipBytes slip;
    RoadTables road;
    SlipForceGlobals globals;
    WearRules wear; // (all off: the wear rules of the races seen)
};
// From the program (its static block and globals: each surface's rolling resistance as the race's start
// 0x35F808 makes it) and the engine's rolling resistance curve (from the same static block).
Tables tables(const Program& program, const ex::Statics& statics);

// Phase 4's tire part after the engine's preparation: the wear's grip, peak forces and slip angles.
void prep_tires(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t);
// Phase 8: slip ratios.
inline void slip_phase(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t) { slip_ratios(c, x.tires, l, t.slip); }
// Phase 10 (0x3535B0) for a car a player drives: the engine's brakes and clutch, driveline, engine and
// gearbox between Gran Turismo 4's tire forces, speed, lateral forces, yaw and hold.
void dynamics(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t, const ex::Statics& s, const ex::GearSettings& g);

} // namespace gt4r::sim::rules
