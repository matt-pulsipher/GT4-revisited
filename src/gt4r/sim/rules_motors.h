#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): layout 7's motors (four wheel drive system 5: the Acura DN-X):
// the engine on the rear axle with a motor of its own, a motor on each front wheel, and a store they charge and draw.

#include "gt4r/sim/rules.h"

namespace gt4r::sim {
class Program;
}

namespace gt4r::sim::rules {

struct WheelLayout;
struct DriveExtra;
struct TireExtra;
struct HybridState;

// The floats 0x355978 (the converter's, for layout 7) makes in 0x8438C0 .. 0x843920 of the program's bytes 0x62045A ..
// 0x620474.
struct MotorTables {
    F front, rear;             // +0 / +1 the front motors' (each) and the rear motor's ratings (kW)
    F front_drive, rear_drive; // +2 / +3 x 10: the front motors' torque factor driving, the rear motor's torque cap
    F front_regen, rear_regen; // +4 / +5 x 10: the same regenerating
    F front_scale;             // +6 x 0.1: the front motors' torque scale
    F capacity;                // +7 x (2 front + rear): the store's capacity
    F front_charge_cap;        // +8 x 5: a front wheel's braking charge a second, at most
    F rear_charge_cap;         // +9 x 5: the rear wheels' braking charge a second, at most
    F idle_rpm;                // +0xA x 100: the open clutch's charging: the rpm below which the throttle rises
    F idle_gain;               // +0xB / 100,000: its throttle per rpm below that
    F idle_share;              // +0xC / 100: its torque at most this share of the full-throttle torque
    F idle_power;              // +0xD: and at most this power (kW)
    F low, high;               // +0xE / +0xF x (2 front + rear) x +7 / 100: the store's marks its share fades between
    F reserve;                 // +0x10 / front: the front motors' share with the store at the low mark
    s8 gear_lo = 0, gear_hi = 0; // +0x11 / +0x12 the gears outside which the rear motor's demand is held at 1
    s8 gear_min = 0;           // +0x13 the rear motor drives only above this gear
    F front_share;             // +0x14 / 100: the front motors' share of the engine's power
    F drive_accel, drive_lat;  // +0x15 / +0x16 / 980: driving, the split's gains on the lateral acceleration and the wanted one
    F drive_dead;              // +0x17 / 100: and its dead band
    F regen_accel, regen_lat;  // +0x18 / +0x19 / 980: the same regenerating
    F regen_dead;              // +0x1A / 100
    F flow_scale;              // 6,000 / (2 front + rear): the flow counter's scale
};
constexpr u32 kMotorBytes = 0x62045A, kMotorTables = 0x8438C0;
MotorTables motor_tables(const u8* bytes); // the 0x1B bytes from kMotorBytes
MotorTables motor_tables(const Program& program);

// The store (car + 0x788, in the hybrid's state's first word) and the flow counter (the signed byte car + 0x78D).
F motor_store(const HybridState& h);
// 0x355DC8 (the car's set-up): the store full, the flow counter and the byte before it 0.
void motor_start(HybridState& h, const MotorTables& t);
// 0x355E88: charge the store by x (at most to its capacity); returns what went in. 0x355F08: draw x (at most what it holds);
// returns what came out. Both count the flow (0x355E40: + x x the flow scale, within -100 .. 100).
F motor_charge(HybridState& h, const MotorTables& t, F x);
F motor_draw(HybridState& h, const MotorTables& t, F x);
// 0x356168: the store's share between its marks (0 .. 1).
F motor_level(const HybridState& h, const MotorTables& t);
// 0x3561D0: the motors' demand: -1 with the throttle (the pedal while shifting) at or below the engine's own share of the
// power; above it twice the rest over the rest of the range x the store's share, held at 1 outside the gears gear_lo ..
// gear_hi, 0 up to gear_min; less 1.
F motor_demand(const ex::Car& c, const HybridState& h, const MotorTables& t);
// 0x3560F0: the engine's throttle with the rear motor beside it: the throttle over the engine's share of the power, at most 1.
F motor_engine_throttle(const ex::Car& c, const MotorTables& t);
// 0x356300: the lateral acceleration the steering asks for (speed² x tan(steer) / wheelbase), at most twice the front tire's
// grip at 1.5 x the steering (curve A), with the steering's sign.
F motor_wanted_lateral(const ex::Car& c, const TireExtra& x, const WheelLayout& l);
// 0x356418 (in the torque vectoring, 0x36D2F8): the front motors' torques on wheels 0 and 1 (driving from the store, or
// regenerating), split by the lateral acceleration against the wanted one; F + 0x51C the power drawn over the front rating.
void front_motors(ex::Car& c, DriveExtra& x, const TireExtra& tires, const WheelLayout& l, const MotorTables& t);
// 0x356BB8 (in the axle step): the rear motor's torque (driving from the store or regenerating into it) and the rear wheels'
// braking charge.
F rear_motor(ex::Car& c, HybridState& h, const MotorTables& t);
// 0x355F80 (the engine's speed with the clutch open): below a full store the engine's throttle raised to charge it at idle, the
// charging torque taken off the engine's.
F idle_charge(ex::Car& c, DriveExtra& x, const MotorTables& t);

} // namespace gt4r::sim::rules
