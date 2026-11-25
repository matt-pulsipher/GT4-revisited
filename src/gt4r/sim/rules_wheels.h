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
// 0x345A50 (phase 16, after the hub offsets): each wheel's speed ramp (wheel +0x40): the hub's speed along and across the
// car (wheel +0x28 / +0x2C) x 0.02 (0x3CA3D70B), at most 1; for the layout's wheels.
void speed_ramp(ex::Car& c, const WheelLayout& l);

// 0x344040 (phase 19, last but the effects): the heading kept within a turn; each wheel's spin angle by its rim speed over
// its axle's tire radius (the layout's axle; nothing while the start holds the car) within a turn, the last step's moved
// with it; the drawn car's points (the body's and the ground's, behind the center of gravity by P + 0x50; a special car's
// raised by the rear tire's radius less the special row's +0x10 and the center of gravity's height along the up axis) and
// the steering's view (0.5 - half the steering over the lock, or for layout kind 1 half F + 0x65C, within 0 .. 1), each with
// its last step's copy; the driver's animation's slots 2 and 3 (car + 0x780 / + 0x784) follow the throttle and the brake
// (0x62075C / 0x620760 of the new value). `special_x10`: the special row's +0x10 (the program's table 0x6205C8);
// `category`: P + 0x32.
void wheel_spin(ex::Car& c, std::array<F, 4>& anim, const WheelLayout& l, F special_x10, u8 category, F x65c, F throttle_share, F brake_share);
constexpr u32 kAnimShares = 0x62075C;

} // namespace gt4r::sim::rules
