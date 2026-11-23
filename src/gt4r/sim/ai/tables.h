#pragma once

// The program's own values the computer drivers read (docs/specs/ai.md "The program's tables"): read
// from the user's CORE.GT4 when a race is set up, never kept in the repository. For a program whose
// tables are not mapped (Prologue, another release) the drivers run on neutral values: no spread
// between drivers, no weaving or keeping apart, the built-in sets of the course's own file only.

#include "gt4r/base/types.h"
#include "gt4r/sim/program.h"

#include <array>
#include <optional>

namespace gt4r::sim::ai {

using gt3r::s8;

// A piecewise-linear table of two points (the program's two-point lookups): y0 at x0 and below, y1
// at x1 and above.
struct Ramp {
    float x0 = 0, x1 = 1, y0 = 0, y1 = 0;
    float at(float x) const;
};

// The tuning block (0x623E50, built at every car's setup by 0x468EE8 from the bytes at 0x623FB8).
struct Tuning {
    u8 smooth_throttle = 0;   // T[0]: 0 the throttle slews in the arcs, else it is set at once
    u8 steer_mode = 1;        // T[2]: below 2 the steering slews to its target
    u8 blend_by_distance = 0; // T[4]: the line change's blend runs off by distance (else by time)
    float blend_rate = 2;     // T+0x74 (m/s): that distance's rate
    u8 offset_flags = 0;      // T[5]: bit 0 hold the line's own side, bit 1 keep apart from cars alongside,
                              // bit 2 come back onto the road
    float side_share = 0.1f, side_share_pit = 1;  // T+0x78 / +0x7C: the bit 0 offset's share
    float offset_rate = 4;    // T+0x80 (m/s): the offset's slew
    float offset_limit = 0.5f; // T+0x84 (m): its limit
    u8 offset_by_grip = 0;    // T[8]: the offset limited by the car's sideways load
    float grip_lo = 0.5f, grip_hi = 0.8f; // T+0x88 / +0x8C: the load's ramp (none at grip_hi and beyond)
    u8 alongside_slow = 0;    // T[9]: corners taken slower with a car alongside
    float alongside_share = 0; // T+0x90
    u8 corner_spread = 0;     // T[0xA]: the corner speeds' random spread
    u8 brake_spread = 0;      // T[0xB]: the braking's random spread
    Ramp corner_lo, corner_hi; // T+0xAC / +0xB8: the spread's bounds by the driver's corner factor
    Ramp brake_lo, brake_hi;   // T+0xDC / +0xE8: the same for braking
    float brake_global = 1;    // T+0xF4: every set's braking factor
    std::array<u8, 4> class_bonus{}; // 0x623FD8: the straight bonus's percent per driveline class (0 or 100: none)
};

struct Tables {
    bool known = false;        // the program's own values (false: the neutral ones)
    Tuning tuning;
    // 0x623DC0: the built-in driver sets, for a course without them (0: paved, 1: loose courses).
    std::array<std::array<u8, 0x40>, 2> builtin{};
    // 0x624030: per requested line, the lines tried in order (-1 ends a row).
    std::array<std::array<s8, 6>, 8> fallback{};
    // 0x624018: the order the lines are prepared in (the heights of one start from the last's).
    std::array<u8, 8> order{6, 0, 1, 2, 5, 3, 4, 7};
    u8 driver_spread = 0;      // 0x620DE9: the computer drivers differ by their grid index
    u8 saturation_cut = 0;     // 0x620DE7: the throttle cut and brake for a steering past the tires' peak

    static Tables load(const Program& program);
};

} // namespace gt4r::sim::ai
