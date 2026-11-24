#pragma once

// What a car's sounds follow, read from the simulation's car record (docs/formats/sound.md "A
// race's sound"): the engine's speed, the throttle and the two torques the levels are made of,
// the clutch, the turbo's spool and blow-off, the slipstream, and each wheel's slip, skid pitch,
// rim speed, surface and width, where the published race::CarState has only part of these.

#include "gt4r/audio/car_sound.h"

namespace gt4r::sim {
class Session;
struct Spec;
} // namespace gt4r::sim

namespace gt4r::audio {

// Where a car's engine sits, as its sounds place it: 1 front, 2 middle, 3 rear. The spec's byte 3
// when it is not 0, else the drive layout (spec byte 0x128: 3 mid-engined, 4 rear-engined).
int engine_place(const sim::Spec& spec);

// Car `i` of the session. With the program's tables, its afterfire's chance too.
CarSoundState sound_state(const sim::Session& session, size_t i, const RaceSoundTables* tables = nullptr);

} // namespace gt4r::audio
