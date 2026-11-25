#pragma once

// The posture of a driver in a race car: from the cockpit's places, the record of what the driver does (DriverRecord, one
// per simulation step) and the car's place, to a solved skeleton. The program's 0x3B2568: the cars whose driver is a
// head-and-shoulders board (cockpit kinds 0x20, 0x21) sit still, looking ahead (0x3B4040); the others follow the record
// (0x3B2708). docs/formats/model.md "The driver in the car".

#include "gt4r/base/types.h"
#include "gt4r/race/posture_solver.h"

#include <array>

namespace gt4r::race::posture {

// What the program makes of a cockpit kind (0x459178).
enum class FigureClass : int {
    None = 0,  // kind 0
    Board = 1, // 0x20, 0x21: a still head and shoulders
    Torso = 2, // 0x10, 0x11: a single-seat racer's driver, no legs
    Whole = 3, // the rest
};
FigureClass figure_class(int cockpit_kind);

// The cockpit as the posture asks for it (NormalCarGeometry over the car's parts block; two cars have their own,
// SpecialCarGeometry 0x67DAE0 for kind 0x0A and MTRGeometry 0x67DC70 for kind 0x0F), in the car's frame.
struct Cockpit {
    int kind = 0;          // info +0x02
    int style = 0;         // 0x402270: 1 lying (kind 0x0A), 2 kind 0x0F, 3 to 5 the lever's kind
    V3 pedal[2]{}, seat{}, wheel{}, column{f(0), f(0), f(1)};
    F wheel_radius{}, recline{}; // parts +0x18, +0x40
    V3 lever{}, place_6c{};      // +0x4C, +0x6C
    V3 lever_pivot{};            // +0x58 and +0x64 (its scale): the lever turns about it
    F lever_scale{};
    V3 place_7c{};               // +0x7C, scale +0x78
    F scale_78{};
    bool handlebars = false;
    V3 grip[2]{}, grip_axis[2]{};
    bool no_heave = false;       // vtable slot 47
};
// Throws FormatError on a car file without an info or a parts block. `co_driver`: the other seat's.
Cockpit read_posture_cockpit(ByteSpan car_file, bool co_driver);

// What the driver does in a step: the program's 64 floats (0x402320 makes them, 0x403260 moves them on), by byte offset.
// Those the posture reads: +0x00 head roll, +0x04 head pitch, +0x08 lean, +0x0C the body's pitch, +0x10 .. +0x18 heave,
// +0x1C the hand's place in a gear change (0 .. 1), +0x20 / +0x24 the lever's turn, +0x28 / +0x2C its grip, +0x30, +0x34 the
// steering wheel's angle, +0x38 / +0x3C the elbows, +0x40 .. +0x54 the hands' grips on the rim (to, from, blend), +0x58 the
// place looked at, +0x64 .. +0x6C the head, +0x70 .. +0x7C off the rim, +0x88 the body's twist, +0x8C / +0x90 the shoulders,
// +0x9C, +0xA8, +0xAC / +0xB0 the wrists, +0xB4, +0xB8, +0xBC, +0xC0 .. +0xD8, +0xE0 the gear change's phase (0 .. 3),
// +0xE4 / +0xE8 the knees.
struct DriverRecord {
    std::array<F, 64> v{};
    F& at(u32 offset) { return v[offset / 4]; }
    F at(u32 offset) const { return v[offset / 4]; }
};
// The record between two steps (0x386818).
DriverRecord blend_records(const DriverRecord& before, const DriverRecord& after, F t);

// The solver's inputs: 0x3B4040's, 0x3B2708's.
struct PostureInput {
    Controls controls;
    Places places;
    Gaze gaze;
    bool has_gaze = true;
};
PostureInput still_posture(const Cockpit& c);
// `pos`, `angles` (pitch, yaw, roll) the car's place and turn; zeros: the figure in the car's own frame.
PostureInput driving_posture(const Cockpit& c, const DriverRecord& r, V3 pos, V3 angles, bool co_driver);

} // namespace gt4r::race::posture
