#pragma once

// A car's record as the program lays it out in memory (0x10A0 bytes, physics.md "The car's record"), into ours: a free run's
// recording keeps the car's at its lap's line (formats/replay.md).

#include "gt4r/sim/record.h"

#include <span>

namespace gt4r::sim {

// Every member of ours that has a place in the car block, from its bytes; the members of the parameter block and those without a
// place stay as they are.
void load_car_record(Record& r, std::span<const gt3r::u8> car);

} // namespace gt4r::sim
