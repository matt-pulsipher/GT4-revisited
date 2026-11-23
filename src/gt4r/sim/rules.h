#pragma once

// Gran Turismo 4's own routines of the race step (docs/specs/physics.md), where they are not the shared
// engine's: each on the engine's mirror of the car record (sim/exact/car_record.h) and, for what that
// record cannot hold, on CarExtra. In the PlayStation 2's arithmetic (base/ee_float.h), in the program's
// order of operations; each is checked against the user's own program by tools/gt4diff (its "taps").
// A routine is named by its address in SCUS-97328 v1.01.

#include "sim/exact/car_record.h"

namespace gt4r::sim::rules {

namespace ex = gt3r::sim::exact;
using gt3r::ee::F;

// The engine's behaviour that Gran Turismo 4's shared routines follow: the latest of the builds the
// engine knows (2002 Tokyo-Geneva: its C library's atan2f is this program's, bit for bit), at 60 steps
// a second.
gt3r::sim::Behaviour behaviour();

// 0x36C9F8: the body's axes from its attitude. The pitch is applied before the roll (the engine's
// applies the roll first): forward = (-sin h cos p, cos h cos p, sin p).
void body_axes(F pitch, F roll, F heading, ex::Vec3& fwd, ex::Vec3& lat, ex::Vec3& up);
inline void body_axes(ex::Car& c) { body_axes(c.pitch, c.roll, c.heading, c.fwd, c.lat, c.up); }

} // namespace gt4r::sim::rules
