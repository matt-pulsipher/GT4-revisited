#pragma once

// Gran Turismo 4's own routines of a car's start (rules.h): a rolling start's places and the car put
// on the road already moving.

#include "gt4r/sim/rules_drive.h"

namespace gt4r::sim::rules {

// The race's rolling start (the single-player conductor's virtual +0xF0, 0x347618): when no car has a
// place or speed of its own, the rule block's +0x6C (s16, metres from the start line, negative before it)
// is the last place's distance, +0x6F (metres) the gap between places and +0x6E the speed (km/h). The
// car at grid place `place` of `count` starts at offset + (count - place - 1) x spacing.
int rolling_start_distance(int offset, int spacing, int count, int place);

// 0x36BF68: the gear for a start at v (m/s): reverse for v < 0; first for a one-speed gearbox (kind 3)
// or a stepless one of kind 1; else the first gear g (1 .. gears - 1) whose up-shift speed less 2,000 rpm
// (first gear) or 500 rpm (the others) is above v at its speed factor; the top gear when none is.
inline int start_gear(const ex::Car& c, const DriveExtra& x, F v) { return gear_for_speed(c, x, v); }

// 0x363868: the car moving at v (m/s) along its heading, as the car's set-up leaves it for a rolling
// start: v at most the car's top speed (P +0xFD8), the yaw rate cleared, the velocity along the forward
// axis, the speed records (0x363538), the gear for v, the engine at v times the gear's speed factor
// (from idle to the rev limit; a stepless gearbox of kind 1 holds it at its hold speed), the turbo
// spooled, the clutch locked and engaged, every wheel's rim and hub speed and both axles' speeds at v.
// `computer`: a computer drives the car (0x463B98), which starts with the throttle open.
void moving_start(ex::Car& c, DriveExtra& x, F v, bool computer);

} // namespace gt4r::sim::rules
