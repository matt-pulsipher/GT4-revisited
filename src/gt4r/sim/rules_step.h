#pragma once

// Gran Turismo 4's own routines (rules_*.h) put into the shared engine's phases for a session's step:
// what is ported runs in place of the engine's, the rest is the engine's (docs/specs/physics.md "What
// runs where").

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_aids.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_steer.h"
#include "gt4r/sim/rules_chassis.h"
#include "gt4r/sim/rules_drive.h"
#include "gt4r/sim/rules_input.h"
#include "gt4r/sim/rules_tires.h"
#include "gt4r/sim/rules_wear.h"
#include "gt4r/sim/rules_effects.h"
#include "gt4r/sim/rules_feel.h"
#include "gt4r/sim/rules_force.h"
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
    AidTables aids;
    SteerTables steer;
    BumpTables bumps; // (the session's: by the course's kind)
    AeroGlobals aero;
    PadTables pad; // the controls' (0x347A00, at the race's start: the course row's byte +0x12 taken as 0)
    SlideTables slide; // (0x366D68 at the race's start)
    FeelTables feel;   // (the view follower's gains, 0x34A4B8's sway constants)
    std::array<F, 9> condition{}; // the engine condition's limits (0x620320: sim::condition_table)
    ForceTables force; // the steering force's (0x353938, at the race's start)
    // The rule block's +0x18 .. +0x1B (conductor + 0xF830): each drive class's extra grip for a car a computer drives, in
    // percent (phase 10). A single race's conductor sets each 0 to 20 (0x353570).
    std::array<s8, 4> class_grip{20, 20, 20, 20};
};
// From the program (its static block and globals: each surface's rolling resistance as the race's start
// 0x35F808 makes it) and the engine's rolling resistance curve (from the same static block).
Tables tables(const Program& program, const ex::Statics& statics);

// The parameter block's words the step's own routines read that the converter's mirror has no member for (until the
// converter makes them all): the lock curves the input phase reads (P +0x58 / +0x94 / +0xD0), a kind 6 wheel's ratio
// (P +0x10D3: 1 for spec +0x210 of 150 or more, 0x361908), the speed-switched part's speeds and rate, the mass less the
// wheels' own (P +0x4C).
void step_params(const Spec& s, Record& r);

// Phase 4's engine and tire part after the engine's preparation: the engine's full-throttle and friction
// torques (the 24-point curve), the wear's grip, peak forces and slip angles.
// `grip`: a computer driver's grip by tire class (0x467470), null for a player's car.
void prep_tires(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t, const std::array<F, 3>* grip = nullptr);
// Phase 8: slip ratios.
inline void slip_phase(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t) { slip_ratios(c, x.tires, l, t.slip); }
// 0x353570: a single race's class grips, each 0 (the event leaves it) 20.
std::array<s8, 4> single_race_class_grip(std::array<s8, 4> event);
// Phase 10 (0x3535B0), a player's car or a computer's: brakes and clutch, the tire forces, the speed along the
// body, the wheels' torques, the driveline, engine and gearbox, the lateral forces, yaw and hold.
void dynamics(ex::Car& c, Extra& x, const WheelLayout& l, const Tables& t, const std::array<F, 3>* grip = nullptr);

} // namespace gt4r::sim::rules
