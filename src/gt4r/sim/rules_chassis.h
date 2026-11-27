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
    u8 aero_mode = 0;           // F + 0x5C3: 2 gives the drag a factor of 20 (0x3F3090) ❓ what sets it
    F speed_part_state;         // F + 0x594: the part that changes with speed, how far it is out (phase 15, 0x3F28B8: not ported)
};
// Every member of ChassisExtra with its place in Gran Turismo 4's record: v(name, index (-1: single), block, offset, member).
template <class X, class V> void chassis_fields(X& x, V&& v) {
    using layout::Block;
    v("mass", -1, Block::Car, layout::kState + 0x4D4, x.mass);
    for (int w = 0; w < 4; ++w) v("w.lat", w, Block::Car, layout::kWheel0 + layout::kWheelSize * u32(w) + 0xD4, x.wheel_lat[size_t(w)]);
    v("yaw_follow", -1, Block::Car, layout::kState + 0x63C, x.yaw_follow), v("yaw_follow_v", -1, Block::Car, layout::kState + 0x640, x.yaw_follow_v);
    v("aero_mode", -1, Block::Car, layout::kState + 0x5C3, x.aero_mode);
    v("speed_part_state", -1, Block::Car, layout::kState + 0x594, x.speed_part_state);
}

// The aerodynamics' globals (0x622308 ..), as the race's start sets them (0x3F2F78): the air's density (the static
// block's byte +0 x 0.1), the drag's and the downforce's shares that a slipstream does not take (+1 and +2 over +0), and the
// slipstream filter's rates up and down (10 over +5 and +6; 1 and 0.25 without them).
struct AeroGlobals {
    F density, drag_share, downforce_share, rate_up, rate_down;
};
constexpr u32 kAeroGlobals = 0x62230C;
AeroGlobals aero_globals(const Program& program);
// 0x3F3090 (phase 4): the slipstream's filter (F + 0x5A0) toward the target (F + 0x59C); the drag, its share a slipstream
// takes off (x 20 in the mode F + 0x5C3 = 2); the downforce with the same, faded out with the flow's angle to the body
// (atan2 of the velocity along the body's up axis over the speed) from 5 to 10 degrees; then the part that changes with
// speed (0x3F2DB8: kinds 1, 2, 5 and 6 add its lift x its state to the rear).
void aero(ex::Car& c, const ChassisExtra& x, u8 speed_part_kind, F speed_part_lift, const AeroGlobals& g);

// 0x351FA8: the speed along the body: the driving forces along each wheel's heading, rolling
// resistance and drag over the mass and the free wheels' (their flag byte 0); then the braking and
// lateral forces' share, which stops the car rather than turn its speed's sign. A wheel whose tire
// takes its lateral force across the wheel (Tire::x1da 1) has it along the wheel's steer angle.
void forward_step(ex::Car& c, const ChassisExtra& x, const TireExtra& t, const WheelLayout& l);
// 0x3522B0 (Prologue's 0x3BAFE0 is the same routine in its wheel layout): the wheel's sideways hold near standstill, 0 for
// a wheel without load (+0x4C), its rim (+0x20) over 5 km/h either way, the hub (+0x28) off the rim by over 2 km/h, or the hub's
// speed across the wheel (by the wheel's angle +0x00, +0x2C) over 4 km/h; else sin(0.9 pi x across) cos(0.36 pi x rim) x load.
F low_speed_hold(const ex::Wheel& w);
// 0x352848: the forces across the car and the yaw: each wheel's tire forces, its camber's force, the
// low-speed hold (0x3522B0, the engine's with 2002 Tokyo-Geneva's constants) and the longitudinal
// force turned into the car's frame, about the levers the wheel layout gives; the yaw rate within a
// turn a second; the lateral acceleration over the mass and over the tires' grip.
// `driver_grip`: the computer driver's grip by tire class (0x467470; null for a player's car: 1).
void lateral_yaw(ex::Car& c, ChassisExtra& x, const TireExtra& t, const WheelLayout& l, bool airborne, const SlipForceGlobals& g,
                 const std::array<F, 3>* driver_grip = nullptr);
// 0x352C90: the hold counter's and the start's stops, a braked car's stop near standstill, else the
// accelerations into the velocity.
void stop_hold(ex::Car& c, const WheelLayout& l, bool airborne);
// The step's airborne flag (0x3535B0): the wheels' loads sum to 0.
bool airborne(const ex::Car& c, const WheelLayout& l);

} // namespace gt4r::sim::rules
