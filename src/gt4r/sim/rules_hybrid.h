#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): the hybrid one-speed box (gearbox kind 3: the Toyota Prius G
// and G Touring), 0x368628, which the axle step (0x36D818) runs in place of the engine and clutch: an engine whose speed
// follows a target of its own, a motor on the driveline, and a store between them.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"

namespace gt4r::sim {
class Program;
}

namespace gt4r::sim::rules {

// The car's hybrid state (car + 0x788; the set-up 0x3683C8 starts it: the store full, the rest 0).
struct HybridState {
    F engine_target;  // +0x00 the speed the engine's speed follows (rad/s)
    F store;          // +0x04 the store's charge, 0 .. 1
    u8 flags = 0;     // +0x08 the display's flags: 1 braking, 2 the engine stopped, 4 the motor driving, 8 the engine running,
                      // 0x10 charging, 0x20 drawing
    u8 mode = 0;      // +0x09 0 a full store, 1 between, 2 low (by the store's eighths: below 6.2 / 1.5, from 6.8 / 2.5)
};
template <class X, class V> void hybrid_fields(X& x, V&& v) {
    using layout::Block;
    v("hybrid.engine_target", -1, Block::Car, 0x788, x.engine_target), v("hybrid.store", -1, Block::Car, 0x78C, x.store);
    v("hybrid.flags", -1, Block::Car, 0x790, x.flags), v("hybrid.mode", -1, Block::Car, 0x791, x.mode);
}

// What 0x368628 reads of the program: its fuel rate by engine speed (0x620BA8 for an up-shift class of 1, 0x620BF8 otherwise),
// the power it asks for by speed (0x620C80 at a throttle of 0.1 and above, 0x620C90 below), and the engine speed that gives a
// power (0x620CA0: made at the car's set-up by 0x368260 from the engine's curve: power in kW against its speed).
struct HybridTables {
    ex::Table<12> fuel_class1, fuel_other, power_up, power_low;
};
HybridTables hybrid_tables(const Program& program);
// 0x368260: the engine's power (kW) against its speed, from 0, of an engine curve (the program makes it once, for the race's
// first hybrid car; ours for each).
ex::Table<12> hybrid_engine_speeds(const ex::Table<24>& torque);

// 0x3683C8: the state at the car's set-up.
void hybrid_start(HybridState& h);

// 0x368628: the box's torque to the driveline for this step (the engine's share plus the motor's), the engine's speed set,
// the fuel counted, the store charged or drawn. `engine_torque` is 0x3554A0 at a throttle.
struct DriveExtra;
F hybrid_box(ex::Car& c, DriveExtra& x, HybridState& h, const HybridTables& t, u16 idle_rpm);

} // namespace gt4r::sim::rules
