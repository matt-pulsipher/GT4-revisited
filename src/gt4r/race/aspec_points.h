#pragma once

// A race's A-spec points (0x3C0FF8, 0x3C1220; docs/formats/race_flow.md "A-spec points").

#include "base/ee_float.h"

#include "gt4r/base/types.h"

#include <cstddef>
#include <span>

namespace gt4r::race {

// The points offered to `car` of a race of cars of these ratings (their parameter blocks' +0xFE0): 0 alone, 250 in a mission,
// else 1 .. 200 by how much the others' mean is over its own. `loose`: a course of kind 2 with a car that is not four wheel drive.
int aspec_points(std::span<const gt3r::ee::F> ratings, size_t car, bool mission, bool loose);

} // namespace gt4r::race
