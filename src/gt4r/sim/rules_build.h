#pragma once

// Gran Turismo 4's own part of a car's record (record.h: what the engine's mirror cannot hold) built
// from our spec, as the program's converter (0x361908) and car set-up make it, for a session's cars.
// tools/gt4diff compares it with the program's converter on every car (run_params).

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"

namespace gt4r::sim {

// The wheel layout's row: the spec's byte +0x02 (0x3594E0, into the parameter block's +0x30).
u8 wheel_layout_index(const Spec& s);

// Fills r.x (the tire records with their 24 surfaces, wear floors and kind bytes; the tire index per
// surface; the widths, axle grips and camber curves from r.car.p; the direct slip and lateral scales;
// the mass) and r.layout. r.car.p must be built (exact_bridge.h) before.
void gt4_extras(const Spec& s, const Program& program, Record& r);

} // namespace gt4r::sim
