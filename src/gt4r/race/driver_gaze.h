#pragma once

// Where a driver looks (the step's 0x40DDC0): the record's +0x58 .. +0x60, the place looked at in the car's frame, follows a
// target the driver picks; the posture turns the neck and head to it. docs/formats/model.md "What the driver does".
//
// Here: the driver's usual way of looking (its state 1, 0x40E710): at its computer driver's aim on the road ahead, within
// 60 degrees of straight ahead and 15 degrees up or down left alone, or straight ahead turned by the front wheels when it
// has none or the wheel has been held straight for 2 s with the aim to a side; and the place's springs toward the target
// (0x40F07C). Not here: the glances (states 2 to 11: at a car close by, aside, at random).

#include "gt4r/race/driver_posture.h"

#include <optional>

namespace gt4r::race::posture {

struct DriverGaze {
    V3 target{};          // the filters' +0x7C .. +0x84: what the driver looks at, in the car's frame
    V3 rate{};            // +0x30 .. +0x38: the place's speed
    F held{}, aim_angle{}; // +0xF4: how long the wheel has stood straight, +0xF8: the aim's angle at the last step
    int state = 1;        // the ints' +0x1C

    // What a step needs: the step's length, the record's steering wheel angle (+0x34), the front wheels' mean angle (the
    // context's +0x118), the computer driver's aim (`aim`: in the car's frame, less the seat's place) when it has one, the
    // first step of a run (the place set at once), a co-driver (slower springs).
    struct Inputs {
        F dt;
        F wheel_angle, front_wheels;
        std::optional<V3> aim;
        bool first = false, co_driver = false;
    };
    // Moves `r`'s +0x58 .. +0x60 on (r: the step's new record).
    void step(const Cockpit& c, const Inputs& in, DriverRecord& r);
};

// 0x40DC08 (through 0x40DD38): the place 6.25 m ahead of the seat, the car's frame turned by `yaw` about y.
V3 ahead_of_seat(const Cockpit& c, F yaw);
// 0x40E900 with 0x40DA48: the aim (in the car's frame, less the seat's place) as the place looked at: brought within 60
// degrees of straight ahead, its height to the eye's within 15 degrees and at most 30 beyond. None behind (more than 45
// degrees past the side) or at the seat.
std::optional<V3> road_target(const Cockpit& c, V3 aim);

} // namespace gt4r::race::posture
