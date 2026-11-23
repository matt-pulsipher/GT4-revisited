#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the ground contact, suspension, attitude and the body's motion.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim::rules {

// What these routines keep that the engine's record has no member for.
struct ChassisExtra {
    F mass; // F + 0x4D4: the car's mass as the step reads it
};
// Every member of ChassisExtra with its place in Gran Turismo 4's record: v(name, index (-1: single), block, offset, member).
template <class X, class V> void chassis_fields(X& x, V&& v) {
    v("mass", -1, layout::Block::Car, layout::kState + 0x4D4, x.mass);
}

// 0x351FA8: the speed along the body: the driving forces along each wheel's heading, rolling
// resistance and drag over the mass and the free wheels' (their flag byte 0); then the braking and
// lateral forces' share, which stops the car rather than turn its speed's sign. A wheel whose tire
// takes its lateral force across the wheel (Tire::x1da 1) has it along the wheel's steer angle.
void forward_step(ex::Car& c, const ChassisExtra& x, const TireExtra& t, const WheelLayout& l);

} // namespace gt4r::sim::rules
