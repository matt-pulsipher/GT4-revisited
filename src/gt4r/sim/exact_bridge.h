#pragma once

// Gran Turismo 4's data in the layouts the shared engine's race step reads (the "exact core",
// engine/gt3r/src/sim/exact: Gran Turismo 3's vehicle step). What the two programs share runs there;
// docs/specs/physics.md "What runs where" lists which rules that leaves as Gran Turismo 3's.
//
//  - the spec: ours (car_spec.h, 0x600 bytes) into the core's (0x3E2 bytes), field by field;
//  - the program's tables: the static physics block from the disc's CORE.GT4, its 24 surfaces folded
//    into the core's 16 rows; the tables not yet mapped in Gran Turismo 4's program are the core's own;
//  - the surfaces: a Gran Turismo 4 surface index as a row of the core's per-surface tables.

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "sim/exact/params_build.h"
#include "sim/exact/program_data.h"

namespace gt4r::sim {

// The row of the core's 16-row surface tables that serves a surface index (ground.h; 0 .. 23), and
// the surface a row stands for.
int core_surface(int surface);
int surface_of_core_row(int row);

// The core's program data: its own tables, with the static physics block taken from `program`
// when it is a known build.
gt3r::sim::exact::ProgramData core_program(const Program& program);

// Our spec as the core's. An engine curve of more than 16 points is thinned to 16 (the points whose
// loss changes the curve least go first); a gearbox of more than 7 gears keeps its first 7.
void core_spec(const Spec& from, gt3r::sim::exact::Spec& to);

} // namespace gt4r::sim
