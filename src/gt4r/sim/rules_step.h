#pragma once

// Gran Turismo 4's own routines (rules_*.h) put into the shared engine's phases for a session's step:
// what is ported runs in place of the engine's, the rest is the engine's (docs/specs/physics.md "What
// runs where").

#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_chassis.h"
#include "gt4r/sim/rules_drive.h"
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
    DriveTables drive;
    ex::BrakeHeatConsts brake_heat; // (0x3F3808 from the static block's +0xAC .. +0xAF)
    BodyGlobals body;
};
// From the program (its static block and globals: each surface's rolling resistance as the race's start
// 0x35F808 makes it) and the engine's rolling resistance curve (from the same static block).
Tables tables(const Program& program, const ex::Statics& statics);

// Phase 4's engine and tire part after the engine's preparation: the engine's full-throttle and friction
// torques (the 24-point curve), the wear's grip, peak forces and slip angles.
void prep_tires(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t, const ex::Condition& cond);
// Phase 8: slip ratios.
inline void slip_phase(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t) { slip_ratios(c, x.tires, l, t.slip); }
// Phase 10 (0x3535B0) for a car a player drives: brakes and clutch, the tire forces, the speed along the
// body, the wheels' torques, the driveline, engine and gearbox, the lateral forces, yaw and hold.
// `computer`: a computer drives the car (our drivers, whose cars the session runs as a player's). The program
// shifts a computer's car at its driver's speeds (0x36BEE0 / 0x36BF28 through 0x3F1E30, the driver's byte +0x83
// / 128): not ported, so its gearbox is the engine's (the converter's down-shift speeds).
void dynamics(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t, bool computer = false);

} // namespace gt4r::sim::rules
