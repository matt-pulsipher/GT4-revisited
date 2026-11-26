#pragma once

// The program's own values the computer drivers read (docs/specs/ai.md "The program's tables"): read
// from the user's CORE.GT4 when a race is set up, never kept in the repository. For a program whose
// tables are not mapped (Prologue, another release) the drivers run on neutral values: no spread
// between drivers, no weaving or keeping apart, the built-in sets of the course's own file only.

#include "base/ee_float.h"
#include "gt4r/base/types.h"
#include "gt4r/sim/program.h"

#include <array>
#include <optional>

namespace gt4r::sim::ai {

using gt3r::s8;

// A piecewise-linear table of two points (the program's two-point lookups): y0 at x0 and below, y1
// at x1 and above.
struct Ramp {
    gt3r::ee::F x0, x1{1.0f}, y0, y1;
    gt3r::ee::F at(gt3r::ee::F x) const; // 0x359568
};

// The tuning block (0x623E50, built at every car's setup by 0x468EE8 from the bytes at 0x623FB8).
struct Tuning {
    u8 smooth_throttle = 0;   // T[0]: 0 the throttle slews in the arcs, else it is set at once
    u8 steer_mode = 1;        // T[2]: below 2 the steering slews to its target, else (racing) the PD steering below
    // The PD steering's gains (T[2] of 2 or more): T+0x10 / +0x14 / +0x18 / +0x1C / +0x20 the slip direction's, the yaw rate's,
    // the line heading's (against the travel), the line's offset and the heading's (against the car's) proportional
    // terms; +0x2C the integrator's own; +0x30 / +0x34 / +0x38 / +0x3C / +0x40 their rates' terms; +0x5C the offset's
    // bound (m). From S[9], S[0xA] and S[0xB .. 0xE], S[0x10 .. 0x16] tenths, S[0x1C] hundredths.
    std::array<gt3r::ee::F, 14> pd{}; // T+0x10 .. +0x44 by 4 (+0x28 unused)
    gt3r::ee::F pd_bound;     // T+0x5C
    u8 blend_by_distance = 0; // T[4]: the line change's blend runs off by distance (else by time)
    gt3r::ee::F blend_rate{2.0f};     // T+0x74 (m/s): that distance's rate
    u8 offset_flags = 0;      // T[5]: bit 0 hold the line's own side, bit 1 keep apart from cars alongside,
                              // bit 2 come back onto the road
    gt3r::ee::F side_share{0.1f}, side_share_pit{1.0f};  // T+0x78 / +0x7C: the bit 0 offset's share
    gt3r::ee::F offset_rate{4.0f};    // T+0x80 (m/s): the offset's slew
    gt3r::ee::F offset_limit{0.5f}; // T+0x84 (m): its limit
    u8 offset_by_grip = 0;    // T[8]: the offset limited by the car's sideways load
    gt3r::ee::F grip_lo{0.5f}, grip_hi{0.8f}; // T+0x88 / +0x8C: the load's ramp (none at grip_hi and beyond)
    u8 alongside_slow = 0;    // T[9]: corners taken slower with a car alongside
    gt3r::ee::F alongside_share; // T+0x90
    u8 corner_spread = 0;     // T[0xA]: the corner speeds' random spread
    u8 brake_spread = 0;      // T[0xB]: the braking's random spread
    Ramp corner_lo, corner_hi; // T+0xAC / +0xB8: the spread's bounds by the driver's corner factor
    Ramp brake_lo, brake_hi;   // T+0xDC / +0xE8: the same for braking
    gt3r::ee::F brake_global{1.0f};    // T+0xF4: every set's braking factor
    std::array<u8, 4> class_bonus{}; // 0x623FD8: the straight bonus's percent per driveline class (0 or 100: none)
};

struct Tables {
    bool known = false;        // the program's own values (false: the neutral ones)
    bool japan = false;        // SCPS-17001 v1.05's program (its own rules where they differ)
    Tuning tuning;
    // 0x623DC0: the built-in driver sets, for a course without them (0: paved, 1: loose courses).
    std::array<std::array<u8, 0x40>, 2> builtin{};
    // 0x624030: per requested line, the lines tried in order (-1 ends a row).
    std::array<std::array<s8, 6>, 8> fallback{};
    // 0x624018: the order the lines are prepared in (the heights of one start from the last's).
    std::array<u8, 8> order{6, 0, 1, 2, 5, 3, 4, 7};
    // B-spec's pace levels (0x3F1790 from the bytes at 0x622088 .. 0x6220A4, five levels): the driver's shift speeds
    // x shift / 128, its braking factor and its arcs' speed factor; the level a B-spec driver and a computer driver start at
    // (0x622080 / 0x622081).
    struct PaceLevel {
        u8 shift = 128;
        gt3r::ee::F brake{1.0f}, corner{1.0f}, x20E0{1.0f};
    };
    std::array<PaceLevel, 5> pace{};
    std::array<u8, 2> pace_start{2, 2};
    u8 driver_spread = 0;      // 0x620DE9: the computer drivers differ by their grid index
    u8 saturation_cut = 0;     // 0x620DE7: the throttle cut and brake for a steering past the tires' peak

    static Tables load(const Program& program);
};

} // namespace gt4r::sim::ai
