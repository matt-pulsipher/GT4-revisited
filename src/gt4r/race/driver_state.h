#pragma once

// What the driver of an open race car does, step by step (the program's 0x370 bytes per driver at the car +0x9A0): the
// record the posture reads (driver_posture.h), made at a run's start (0x402320) and moved on every simulation step
// (0x403260). docs/formats/model.md "What the driver does".
//
// Of the step: the steering wheel's angle (0x403F90), the hands that go round with it and take new grips (0x410E60 ->
// 0x40FC78, 0x411150), the elbows (0x406388) and the clavicles (0x4053A8) of a car with a wheel (cockpit style 0). Not
// here: the gaze, the lean, the heave, the gear change, the feet.

#include "gt4r/race/driver_posture.h"

#include <array>

namespace gt4r::race::posture {

// The program's table of a hand's speed over its way (0x622A20, eleven shares).
using HandSpeeds = std::array<F, 11>;

struct DriverState {
    DriverRecord before, after; // the step before's (+0x260), the last one's (+0x160)
    // The filter block (+0x40): the hands' angles on the wheel (+0x90), their ways (+0x88), how long they have been close
    // together or crossed (+0x74), the wheel's last four rates (+0xAC).
    F hand[2], way[2], idle;
    std::array<F, 4> rates{};
    u8 state[2] = {0, 0}, was[2] = {0, 0}; // the hands' states (+0x30): 0 hold, 1 at the end of the turn, 2 / 3 going ...

    // At rest in the cockpit (0x402320).
    void reset(const Cockpit& c);
    // A step of `dt` seconds; `follow`: the steering command's follower (F +0x664).
    void step(const Cockpit& c, F follow, F dt, const HandSpeeds& speeds);

private:
    void hand_step(int h, bool regrip, F turned, F dt, F rate, const HandSpeeds& speeds);
};

} // namespace gt4r::race::posture
