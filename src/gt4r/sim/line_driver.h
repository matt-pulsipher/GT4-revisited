#pragma once

// A plain driver for trying a session out: the pedal a caller gives and steering along the middle of
// the road. Not a computer driver of the game's: those follow the courses' driving lines.

#include "gt4r/sim/session.h"

namespace gt4r::sim {

class LineDriver {
public:
    // The driver of car `car` of a session.
    LineDriver(const Session& session, size_t car);
    // The input for the next step: steering towards the road's middle a little ahead (further at
    // speed), `throttle` on the pedal; `brake_for_turns` eases off and brakes where the road ahead bends.
    race::CarInput drive(float throttle = 1.0f, bool brake_for_turns = false);

private:
    const Session* session_;
    size_t car_;
    int road_ = 0;      // the road's material: the one under the car at the start
    Vec3 last_aim_{};
    bool has_aim_ = false;
};

} // namespace gt4r::sim
