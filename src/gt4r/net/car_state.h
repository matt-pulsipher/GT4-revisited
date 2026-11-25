#pragma once

// A car's state as it travels between the machines of an online race (docs/specs/net.md "Race"): the shared engine's
// net::CarSnapshot, filled from and put into Gran Turismo 4's record of the car. Everything Gran Turismo 4 needs fits it:
// the motion's fields are the same quantities (the offsets are this game's record's, F + ..), the driver's frame is the
// first 16 of its 32 bytes, its nitrous button in the frame's kind (bit 3).

#include "gt4r/race/simulation.h"
#include "gt4r/sim/session.h"
#include "net/protocol.h"

namespace gt4r::net {

using gt3r::net::CarEntry;
using gt3r::net::CarMotion;
using gt3r::net::CarSnapshot;
using gt3r::net::PadInput;

// The race clock in a state: 3000 units a second, 50 a step (the race's own clock; negative in the countdown).
constexpr s32 kClockPerStep = 50;

// The motion of a car's record, and the record with a motion put in: position, velocity, attitude, yaw rate, engine speed,
// steering command and axle speeds bit for bit, the rates, wheel speeds and suspension in their fixed units. A field whose
// own value is what the motion says (within its unit) stays as it is, so a prediction that has run as its owner's car did is
// not touched. Returns the distance the car was moved (metres).
CarMotion car_motion(const sim::Session& s, size_t car);
float put_motion(sim::Session& s, size_t car, const CarMotion& m);

// The driver's frame of a step: as the input phase reads it, as it travels, and back. A computer's or B-spec driver's
// outputs (race::CarInput::computer) travel as a frame of throttle and brake; its steering is the motion's command.
PadInput pad_input(const race::CarInput& in);
race::PadRecord pad_record(const PadInput& p);
// The input a prediction takes from a state: its frame (`pad` holds the bytes and must live through the step), or a
// computer's outputs. `keep_shift`: the frame's shift too (a step the owner has run; never in a step ahead of it).
race::CarInput driver_input(const PadInput& p, const CarEntry& e, const CarMotion& m, race::PadRecord& pad, bool keep_shift);

// What of a car's race entry its prediction reads: the gearbox's style, the wheel's kind, the aids, who drives.
CarEntry car_entry(const sim::Session& s, size_t car, bool asm_on, bool tcs_on);

// A car's state after a step, `clock` the race's (units of 1/3000 s); `inputs` this step's frame first, then older ones.
CarSnapshot make_snapshot(const sim::Session& s, size_t car, const CarEntry& entry, u32 tick, s32 clock, int laps, const PadInput* inputs, int n);
// Whether a state is one a car of `gears` forward gears can be in (a modified game can send anything): its place and speeds within
// what a course and a car hold, its gear and clutch ones the step knows. Its frames' odd controller kinds are read as a pad's.
bool sane(CarSnapshot& s, int gears);
// What the wire leaves out: the race time, the place and velocity in the course's frame, the speed.
void complete_snapshot(CarSnapshot& s);

} // namespace gt4r::net
