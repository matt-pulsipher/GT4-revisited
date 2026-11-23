#pragma once

// Gran Turismo 4's own routines of the race step (docs/specs/physics.md), where they are not the shared
// engine's: each on our record (record.h: the engine's mirror of the car record and, for what that
// cannot hold, each area's extra part). In the PlayStation 2's arithmetic (base/ee_float.h), in the
// program's order of operations; each is checked against the user's own program by tools/gt4diff (its
// "taps"). A routine is named by its address in SCUS-97328 v1.01.

#include "sim/exact/car_record.h"
#include "sim/exact/tick.h"

#include <array>

namespace gt4r::sim {
class Program;
}

namespace gt4r::sim::rules {

namespace ex = gt3r::sim::exact;
using gt3r::ee::F;
using gt3r::u8;
using gt3r::u32;
using gt3r::s8;
using gt3r::s32;

// The engine's behaviour that Gran Turismo 4's shared routines follow: the latest of the builds the
// engine knows (2002 Tokyo-Geneva: its C library's atan2f is this program's, bit for bit), at 60 steps
// a second.
gt3r::sim::Behaviour behaviour();

// A car's wheels (the program's table 0x620478, 0x54 bytes a row, the row the parameter block's byte
// +0x30 names; 0x359510 / 0x359538 return it).
struct WheelLayout {
    u8 kind = 0;                                // +0x00 (1: the camber's grip is read at 0)
    u8 count = 4;                               // +0x01 the wheels the step runs
    std::array<u8, 8> axle{0, 0, 1, 1};         // +0x14 + w: wheel w's axle (0 front, 1 rear)
    std::array<u8, 8> side{0, 1, 0, 1};         // +0x1C + w: wheel w's side (0 left)
    std::array<u8, 8> mean_slip{};              // +0x3C + w: above 0 (signed): wheel w's slip angle is in the mean the step keeps (0x352408)
    std::array<u8, 8> camber_thrust{};          // +0x44 + w: 2: wheel w's camber gives a lateral force (0x352408)
};
constexpr u32 kWheelLayouts = 0x620478, kWheelLayoutSize = 0x54;
// The row of the table at `row` (its 0x54 bytes).
WheelLayout wheel_layout(const u8* row);
WheelLayout wheel_layout(const Program& program, u8 index);

// 0x359830: y(v) on a table, linear between its points (the engine's 0x1D8FB8, the same arithmetic).
template <size_t N> F table(const ex::Table<N>& t, F v) { return ex::lookup(t, v); }

// 0x36C9F8: the body's axes from its attitude. The pitch is applied before the roll (the engine's
// applies the roll first): forward = (-sin h cos p, cos h cos p, sin p).
void body_axes(F pitch, F roll, F heading, ex::Vec3& fwd, ex::Vec3& lat, ex::Vec3& up);
inline void body_axes(ex::Car& c) { body_axes(c.pitch, c.roll, c.heading, c.fwd, c.lat, c.up); }

} // namespace gt4r::sim::rules
