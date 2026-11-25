#pragma once

// The course's cones and boards: their kinds (GDGT, course slot 47) and where they stand (the road
// file's gadget records, RNW4 +0x44 / +0x70). docs/formats/course.md "Cones and boards".

#include "gt4r/base/types.h"
#include "gt4r/formats/course/runway.h"

#include <array>
#include <vector>

namespace gt4r::race {

struct Gadgets {
    struct Kind {
        int index = 0;               // byte +0x00
        u8 flags = 0;                // byte +0x02: bit 0, a body that meets a car once and then no more (0x3E3C48 latches it)
        int hull_points = 0;         // byte +0x03
        std::array<float, 3> inertia{}; // +0x04
        std::vector<course::Vec3> hull; // +0x10, x y z w each
        float radius = 0, mass = 0;  // +0x90, +0x94
    };
    struct Placement {
        int kind = 0;
        course::Vec3 position;
        std::array<float, 3> angles{}; // radians: the pylons' Euler angles (a, b, c)
    };
    std::vector<Kind> kinds;
    std::vector<Placement> placements;
    ByteSpan models; // the kinds' model set (MDLS at GDGT +0x400): model 2k the kind's shape, 2k + 1 its shadow 🔎

    static bool is_gadgets(ByteSpan d) { return d.size() >= 0x20 && gt3r::has_magic(d, "GDGT"); }
    // `gdgt` slot 47, `rnw4` slot 55 (either may be empty). Throws FormatError.
    static Gadgets parse(ByteSpan gdgt, ByteSpan rnw4);
    // A placement's turn (row-major 3 x 3, body to course), as the shared engine's pylons build it.
    static std::array<float, 9> rotation(const std::array<float, 3>& angles);
};

} // namespace gt4r::race
