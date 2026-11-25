#pragma once

// What the driver of an open race car does, step by step (the program's 0x370 bytes per driver at the car +0x9A0): the
// record the posture reads (driver_posture.h), made at a run's start (0x402320) and moved on every simulation step
// (0x403260). docs/formats/model.md "What the driver does".

#include "gt4r/race/driver_posture.h"

namespace gt4r::race::posture {

struct DriverState {
    DriverRecord before, after; // +0x160, +0x260
    // At rest in the cockpit (0x402320).
    void reset(const Cockpit& c);
};

} // namespace gt4r::race::posture
