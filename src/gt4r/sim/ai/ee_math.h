#pragma once

// Gran Turismo 4's own float routines the drivers call where they differ from the shared engine's (base/ee_math.h).

#include "base/ee_float.h"

namespace gt4r::sim::ai {

// atan2f (0x57D118): 0 at the origin; the octant from the signs and the larger magnitude, atanf (0x57D008) of the
// smaller over the larger, then turned into place.
gt3r::ee::F atan2(gt3r::ee::F y, gt3r::ee::F x);

} // namespace gt4r::sim::ai
