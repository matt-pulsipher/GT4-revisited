#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the ground contact, suspension, attitude and the body's motion.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim::rules {

// What these routines keep that the engine's record has no member for.
struct ChassisExtra {
    F mass;                     // F + 0x4D4: the car's mass as the step reads it
    std::array<F, 4> wheel_lat; // wheel +0xD4: the wheel's force across the car
    F yaw_follow, yaw_follow_v; // F + 0x63C / + 0x640: a follower of the yaw acceleration (0x350D40)
};
// Every member of ChassisExtra with its place in Gran Turismo 4's record: v(name, index (-1: single), block, offset, member).
template <class X, class V> void chassis_fields(X& x, V&& v) {
    using layout::Block;
    v("mass", -1, Block::Car, layout::kState + 0x4D4, x.mass);
    for (int w = 0; w < 4; ++w) v("w.lat", w, Block::Car, layout::kWheel0 + layout::kWheelSize * u32(w) + 0xD4, x.wheel_lat[size_t(w)]);
    v("yaw_follow", -1, Block::Car, layout::kState + 0x63C, x.yaw_follow), v("yaw_follow_v", -1, Block::Car, layout::kState + 0x640, x.yaw_follow_v);
}

// 0x351FA8: the speed along the body: the driving forces along each wheel's heading, rolling
// resistance and drag over the mass and the free wheels' (their flag byte 0); then the braking and
// lateral forces' share, which stops the car rather than turn its speed's sign. A wheel whose tire
// takes its lateral force across the wheel (Tire::x1da 1) has it along the wheel's steer angle.
void forward_step(ex::Car& c, const ChassisExtra& x, const TireExtra& t, const WheelLayout& l);
// 0x352848: the forces across the car and the yaw: each wheel's tire forces, its camber's force, the
// low-speed hold (0x3522B0, the engine's with 2002 Tokyo-Geneva's constants) and the longitudinal
// force turned into the car's frame, about the levers the wheel layout gives; the yaw rate within a
// turn a second; the lateral acceleration over the mass and over the tires' grip.
// `driver_grip`: the computer driver's grip on a surface (0x467470; 1 for a player's car).
void lateral_yaw(ex::Car& c, ChassisExtra& x, const TireExtra& t, const WheelLayout& l, bool airborne, const SlipForceGlobals& g,
                 F driver_grip = F(1.0f));
// 0x352C90: the hold counter's and the start's stops, a braked car's stop near standstill, else the
// accelerations into the velocity.
void stop_hold(ex::Car& c, const WheelLayout& l, bool airborne);
// The step's airborne flag (0x3535B0): the wheels' loads sum to 0.
bool airborne(const ex::Car& c, const WheelLayout& l);

} // namespace gt4r::sim::rules
