#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the engine, clutch, gearbox and driveline.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_fuel.h"
#include "gt4r/sim/rules_hybrid.h"
#include "gt4r/sim/rules_motors.h"
#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim::rules {

// The engine block (P + 0x134): the engine's (sim/exact/car_record.h Engine, P + 0x138 there) with a
// curve of 24 points; what follows the curve is 0x40 further on.
struct EngineBlock {
    ex::Table<24> torque;                  // +0x00 (x +0x0C, y +0x6C) full-throttle torque by engine speed
    u16 rev_limit = 0, idle = 0;           // +0xCC / +0xCE rpm
    F friction;                            // +0xD0 friction torque per rad/s
    std::array<gt3r::s16, 2> turbo_knee{}; // +0xD4 per stage
    std::array<u8, 2> turbo_shape{};       // +0xD8
    u8 power_cap_on = 0;                   // +0xDB
    F power_cap, power_cap_soft;           // +0xDC / +0xE0
    std::array<F, 2> turbo_peak;           // +0xE4
    std::array<F, 2> turbo_rate;           // +0xEC
    std::array<F, 2> turbo_coef;           // +0xF4
    F boost_max;                           // +0xFC
};
constexpr u32 kEngineBlock = 0x134;

// The driveline block (P + 0x234): the engine's (Driveline, P + 0x1F8 there) with eight down-shift speeds
// (gears 2 .. 9), the ratios and speed factors of reverse and nine gears, then the rest 0x14 further on.
struct DrivelineBlock {
    u8 layout = 0;                         // +0x00 the drive's layout (0 .. 10)
    u8 unequal = 0;                        // +0x01 the axles' tires differ: both axles by the driven axle's radius
    u8 gears = 0;                          // +0x02 forward gears
    u8 driven_axle = 0;                    // +0x03
    F awd_k, awd_capacity;                 // +0x04 / +0x08
    F engine_inertia;                      // +0x0C
    std::array<u8, 2> diff_type{};         // +0x10 per axle
    u8 kind = 0;                           // +0x12 the gearbox: 1 / 2 without steps, 3 one gear, 4 ...
    u8 x13 = 0;                            // +0x13 (4 for the car category 5)
    std::array<F, 2> diff_initial, diff_accel, diff_decel; // +0x14 / +0x1C / +0x24
    u16 launch_rpm = 0, up_rpm = 0;        // +0x2C / +0x2E
    std::array<u16, 8> down_rpm{};         // +0x30 the down-shift from gear g + 2 at g
    std::array<F, 10> ratio;               // +0x40 reverse, gears 1 .. 9
    std::array<F, 10> gear_speed;          // +0x68 engine rad/s per m/s
    std::array<F, 2> tire_radius, inv_tire_radius, width; // +0x90 / +0x98 / +0xA0
    F shift_clutch_torque;                 // +0xB0
    std::array<F, 2> wheel_gain;           // +0xB4
    std::array<F, 2> axle_inertia;         // +0xBC
    F inv_axle_inertia, inv_engine_inertia; // +0xC4 / +0xC8
    u16 built_down_rpm0 = 0;               // (not the block's) +0x30 as 0x34ACA8 left it, before a stepless box of kind 1's
                                           // hold speed (0x360780): the settings page's gear chart's (sim::build_record sets it)
};
constexpr u32 kDriveline = 0x234;

// What these routines keep that the engine's record has no member for.
struct DriveExtra {
    EngineBlock e;
    DrivelineBlock d;
    F ratio_word;                // F +0x518 a stepless gearbox's ratio (1 locked in a gear)
    std::array<gt3r::s8, 4> marks{}; // car +0x774 (0x35A5F8): [0] the last gear change, +1 up, -1 down, 0 none
    // The fuel and the nitrous (rules_fuel.h), which the engine's torque counts as it runs (mutable: the routines that take the
    // torque read the rest of this block only).
    mutable FuelState fuel;
    F rating;                    // P + 0xFE0 the car's rating (0x3F86E8, the converter's last: the B-spec skill's class)
    F top_speed;                 // P + 0xFD8 the converter's top speed (0x360520)
    HybridState hybrid;          // car + 0x788 the hybrid one-speed box's state (gearbox kind 3)
    u8 shift_factor = 128;       // car +0x83: a B-spec driver's shift speeds x this / 128 (0x3F1E30; Session::set_shift_factor)
};

// Every member of DriveExtra with its place in Gran Turismo 4's record: v(name, index (-1: single), block, offset, member).
template <class X, class V> void drive_fields(X& x, V&& v) {
    using layout::Block;
    const Block P = Block::Params;
    auto& e = x.e;
    const u32 E = kEngineBlock;
    v("e.torque.n", -1, P, E + 0x00, e.torque.n);
    for (u32 k = 0; k < 24; ++k) v("e.torque.x", int(k), P, E + 0x0C + 4 * k, e.torque.x[k]), v("e.torque.y", int(k), P, E + 0x6C + 4 * k, e.torque.y[k]);
    v("e.rev_limit", -1, P, E + 0xCC, e.rev_limit), v("e.idle", -1, P, E + 0xCE, e.idle), v("e.friction", -1, P, E + 0xD0, e.friction);
    for (u32 s = 0; s < 2; ++s) {
        v("e.turbo_knee", int(s), P, E + 0xD4 + 2 * s, e.turbo_knee[s]), v("e.turbo_shape", int(s), P, E + 0xD8 + s, e.turbo_shape[s]);
        v("e.turbo_peak", int(s), P, E + 0xE4 + 4 * s, e.turbo_peak[s]), v("e.turbo_rate", int(s), P, E + 0xEC + 4 * s, e.turbo_rate[s]);
        v("e.turbo_coef", int(s), P, E + 0xF4 + 4 * s, e.turbo_coef[s]);
    }
    v("e.power_cap_on", -1, P, E + 0xDB, e.power_cap_on), v("e.power_cap", -1, P, E + 0xDC, e.power_cap);
    v("e.power_cap_soft", -1, P, E + 0xE0, e.power_cap_soft), v("e.boost_max", -1, P, E + 0xFC, e.boost_max);
    auto& d = x.d;
    const u32 D = kDriveline;
    v("d.layout", -1, P, D + 0x00, d.layout), v("d.unequal", -1, P, D + 0x01, d.unequal), v("d.gears", -1, P, D + 0x02, d.gears);
    v("d.driven_axle", -1, P, D + 0x03, d.driven_axle), v("d.awd_k", -1, P, D + 0x04, d.awd_k), v("d.awd_capacity", -1, P, D + 0x08, d.awd_capacity);
    v("d.engine_inertia", -1, P, D + 0x0C, d.engine_inertia), v("d.kind", -1, P, D + 0x12, d.kind), v("d.x13", -1, P, D + 0x13, d.x13);
    for (u32 a = 0; a < 2; ++a) {
        v("d.diff_type", int(a), P, D + 0x10 + a, d.diff_type[a]), v("d.diff_initial", int(a), P, D + 0x14 + 4 * a, d.diff_initial[a]);
        v("d.diff_accel", int(a), P, D + 0x1C + 4 * a, d.diff_accel[a]), v("d.diff_decel", int(a), P, D + 0x24 + 4 * a, d.diff_decel[a]);
        v("d.tire_radius", int(a), P, D + 0x90 + 4 * a, d.tire_radius[a]), v("d.inv_tire_radius", int(a), P, D + 0x98 + 4 * a, d.inv_tire_radius[a]);
        v("d.width", int(a), P, D + 0xA0 + 4 * a, d.width[a]), v("d.wheel_gain", int(a), P, D + 0xB4 + 4 * a, d.wheel_gain[a]);
        v("d.axle_inertia", int(a), P, D + 0xBC + 4 * a, d.axle_inertia[a]);
    }
    v("d.launch_rpm", -1, P, D + 0x2C, d.launch_rpm), v("d.up_rpm", -1, P, D + 0x2E, d.up_rpm);
    for (u32 g = 0; g < 8; ++g) v("d.down_rpm", int(g), P, D + 0x30 + 2 * g, d.down_rpm[g]);
    for (u32 g = 0; g < 10; ++g) v("d.ratio", int(g), P, D + 0x40 + 4 * g, d.ratio[g]), v("d.gear_speed", int(g), P, D + 0x68 + 4 * g, d.gear_speed[g]);
    v("d.shift_clutch_torque", -1, P, D + 0xB0, d.shift_clutch_torque);
    v("d.inv_axle_inertia", -1, P, D + 0xC4, d.inv_axle_inertia), v("d.inv_engine_inertia", -1, P, D + 0xC8, d.inv_engine_inertia);
    const Block C = Block::Car;
    v("ratio_word", -1, C, layout::kState + 0x518, x.ratio_word);
    for (u32 k = 0; k < 4; ++k) v("marks", int(k), C, 0x774 + k, x.marks[k]);
    fuel_fields(x.fuel, v);
    v("shift_factor", -1, C, 0x83, x.shift_factor);
    hybrid_fields(x.hybrid, v);
    v("rating", -1, P, 0xFE0, x.rating), v("top_speed", -1, P, 0xFD8, x.top_speed);
}

// The race's tables these routines read (the program's globals): the four wheel drive system 5's fade
// (0x62030C the lateral acceleration it starts at, 0x620314 the factor at 1 g and beyond; 0x62030A 1: a
// negative torque gives minus the full transfer) and the stepless gearboxes' rates (0x3F2070 makes them
// once from the bytes at 0x6220F4 / F5 / 0x622100 / 01, x 0.01: kind 1 at full throttle and released,
// kind 2 towards a lower and a higher ratio); the controller settings (0x622110: +0x08 the automatic
// box's mode, +0x09 .. +0x0B the down-shift's offsets by the throttle, its lift and the brake, x 100 rpm).
struct DriveTables {
    ex::Awd3 awd;
    ex::Stepless stepless;
    ex::GearSettings gears;
    HybridTables hybrid; // (the hybrid one-speed box's, rules_hybrid.h)
    MotorTables motors;  // (layout 7's motors, rules_motors.h: 0x355978's)
};
constexpr u32 kAwdFade = 0x62030A, kSteplessBytes = 0x6220F4, kControllerSettings = 0x622110;
// From the bytes at those places (14, 14 and 12 of them), or from the program.
DriveTables drive_tables(const u8* awd, const u8* stepless_bytes, const u8* settings);
DriveTables drive_tables(const Program& program);
DriveTables drive_tables_bytes(const Program& program); // (without the hybrid's tables)

// 0x34EB00: a turbo stage's boost at rpm n.
F turbo_boost(const EngineBlock& e, gt3r::s16 n, int stage);
// 0x350DA0: the speed of the axles the engine drives (by the driveline's layout: 0, 4 .. 7 the rear, 1,
// 2 and 9 the front, 3 and 10 their mean, 8 the rear's plus the split times the difference).
F driven_axle_speed(const ex::Car& c, const DrivelineBlock& d);
// 0x350E28: the clutch's wheel side: the driven axles' speed times the gear's ratio (a stepless box of
// kind 2: its second gear's from the second on), not below 0.
F wheel_side_speed(const ex::Car& c, const DrivelineBlock& d);
// 0x3554A0: the engine's torque at throttle x: the turbo's lag and boost, the blow-off, the rev
// limiter (0x355410), the power cap (0x34EA68); the turbo's spool level.
F engine_torque(ex::Car& c, const DriveExtra& x, F throttle);
// 0x355410: the rev limiter (on at the rev limit, off 500 rpm below it) and the throttle's torque less the friction under the
// power cap.
F engine_output_torque(ex::Car& c, const DriveExtra& x, F throttle);
// 0x34E9A0 / 0x34EA08 (phase 4): the full-throttle torque at the engine's speed (the 24-point curve)
// times the condition's factor, and the friction torque.
void engine_torques(ex::Car& c, const DriveExtra& x, const ex::Condition& cond);
// 0x3511A0: the clutch (0x350FB8; a one-speed box 0x351178; kind 4 always locked), each wheel's brake
// torque by its axle, the handbrake on the wheels behind the center of gravity, the wheels' flags.
void brakes_and_clutch(ex::Car& c, DriveExtra& x, const WheelLayout& l);
// 0x36D230: each wheel's net torque from its tire's reaction and its brake.
void wheel_torques(ex::Car& c, const DriveExtra& x, const WheelLayout& l);
// 0x36D2F8: torque vectoring of the y / t differentials, per wheel group (layout 7: first its front motors, 0x356418:
// front_motors, rules_motors.h).
void vectoring(ex::Car& c, const DriveExtra& x, const WheelLayout& l);
// 0x36D818: the axle step: the clutch's torque, the four wheel drive's coupling and center clutch, the
// axles' speeds.
void axle_step(ex::Car& c, DriveExtra& x, const WheelLayout& l, const DriveTables& t);
// 0x36E4B8: the wheels' speeds through each group's differential.
void wheel_speeds(ex::Car& c, const DriveExtra& x, const WheelLayout& l);
// 0x3519B8: the engine's speed (open, locked or slipping clutch) and rpm (0x350F48).
void engine_speed(ex::Car& c, DriveExtra& x, const WheelLayout& l, const DriveTables* t = nullptr);
// 0x36BF68: the gear for speed v (rules_start.h's start_gear on these blocks).
int gear_for_speed(const ex::Car& c, const DriveExtra& x, F v);
// 0x351DC8: the gear decision by the pedal style (0x36C8E8: automatic 0x36C160, manual 0x36C690,
// sequential 0x36C780 / 0x36C850) and the change, the clutch to slip or shift.
void gear_step(ex::Car& c, DriveExtra& x, const WheelLayout& l, const TireExtra& tires, const DriveTables& t);

} // namespace gt4r::sim::rules
