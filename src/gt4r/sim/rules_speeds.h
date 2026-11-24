#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): phase 19's speeds and bookkeeping (0x35DE20): the speeds
// over the ground, each hub's velocity by the wheel layout, the odometer, the engine's condition, the speedometer, a
// follower of the lateral acceleration and the free-rolling wheels.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"

namespace gt4r::sim {
struct ConditionLimits;
}

namespace gt4r::sim::rules {

// What 0x35DE20 reads of the program's data, as a race's start leaves it.
struct SpeedTables {
    F limits[9];         // the engine condition's limits (0x620320; sim::condition_limits)
    F dirt_max;          // 0x620B18: the condition block's +0x10 word's greatest (0x366100 at the race's start: the word
                         // 0x620AF6 x 1000)
    u32 lean_on = 0;     // 0x620788: a two-wheeler's lean in its front hub's sideways speed (0 in the image)
    u32 roll_on = 0;     // 0x620784: the roll rate in each hub's sideways speed (0 in the image)
    std::array<F, 2> lean_row{}; // the special row's +0x30 / +0x34 (0x359F50)
};
SpeedTables speed_tables(const Program& program, u8 special_row);

// What these routines keep that the engine's record has no member for.
struct SpeedExtra {
    F speedo;              // F + 0x5CC the speedometer (|speed| followed at 111.1 m/s per second)
    F g_follow, g_follow_v; // F + 0x560 / + 0x55C a follower of the lateral acceleration and its rate
    F drive_distance;      // F + 0x6AC the distance driven while control source 1 (wraps at 80,000 km)
};
template <class X, class V> void speeds_fields(X& x, V&& v) {
    using layout::Block;
    v("speeds.speedo", -1, Block::Car, layout::kState + 0x5CC, x.speedo);
    v("speeds.g_follow", -1, Block::Car, layout::kState + 0x560, x.g_follow);
    v("speeds.g_follow_v", -1, Block::Car, layout::kState + 0x55C, x.g_follow_v);
    v("speeds.drive_distance", -1, Block::Car, layout::kState + 0x6AC, x.drive_distance);
}

// 0x35D920 / 0x35D6B0: the speeds along and across the ground's frame, each hub's velocity (its yaw lever from the
// layout's geometry, side and fore-aft bytes).
void hub_velocities(ex::Car& c, const WheelLayout& l, const SpeedTables& t, F in_line_x480);
// 0x35D990: wheels neither braked nor driven nor off the ground roll with it; each wheel group's axle speed from its
// wheels' mean (a two-axle car with unequal tires: by the driven axle's radius). Nothing while a layout-3 car is held
// still by the handbrake.
void free_wheels(ex::Car& c, const WheelLayout& l);
// 0x35DE20 (phase 19): all of it. The engine's condition (the record's block at F + 0x5D8: its odometer, wear and oil
// words, +0x10, and +0x14 set for a fresh engine that does not wear) advances by the distance of the step.
void speeds(ex::Car& c, SpeedExtra& x, const WheelLayout& l, const SpeedTables& t, F in_line_x480);

} // namespace gt4r::sim::rules
