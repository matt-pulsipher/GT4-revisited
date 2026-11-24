#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): where each wheel's hub is, by the car's wheel layout (the
// engine's routines place four wheels at the corners; a layout of three wheels, or of two in line, places them its own way).

#include "gt4r/sim/rules.h"

namespace gt4r::sim::rules {

// 0x3458D0 (phase 16, and in 0x345A20): each wheel's hub offset across and along the ground (wheel +0x0C / +0x10): its
// axle's half track (the layout's byte +0x2C + w names the axle) along the body's lateral axis, minus for a left wheel
// (byte +0x1C + w 0), plus for a right one (1), none for a wheel in the middle; then its axle's distance from the center
// of gravity along the forward axis, ahead (byte +0x3C + w above 0) or behind (below 0).
void hub_offsets_xy(ex::Car& c, const WheelLayout& l);
// 0x3457B0: the same for the height (wheel +0x14).
void hub_offsets_z(ex::Car& c, const WheelLayout& l);
// 0x345A20 (phase 19): both.
inline void hub_offsets(ex::Car& c, const WheelLayout& l) { hub_offsets_z(c, l), hub_offsets_xy(c, l); }

} // namespace gt4r::sim::rules
