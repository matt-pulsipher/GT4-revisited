#pragma once

// What the driver of an open race car does, step by step (the program's 0x370 bytes per driver at the car +0x9A0): the
// record the posture reads (driver_posture.h), made at a run's start (0x402320) and moved on every simulation step
// (0x403260). docs/formats/model.md "What the driver does".
//
// Of the step: the steering wheel's angle (0x403F90), the nod (0x4043C8), the lean (0x404890) and the heave (0x4051E0) by
// the seat's acceleration, the hands that go round with the wheel and take new grips (0x410E60 -> 0x40FC78, 0x411150), the
// elbows (0x406388) and the clavicles (0x4053A8) of a car with a wheel (cockpit style 0), the gaze's usual way (0x40DDC0,
// driver_gaze.h), the hand on the gear lever and the left foot (0x411FB0 → 0x412018, driver_gear.h). Not here: the gaze's
// glances, the right foot.

#include "gt4r/race/driver_gaze.h"
#include "gt4r/race/driver_gear.h"
#include "gt4r/race/driver_posture.h"

#include <array>

namespace gt4r::race::posture {

// The program's table of a hand's speed over its way (0x622A20, eleven shares).
using HandSpeeds = std::array<F, 11>;

// The gear change's inputs at a step (0x412018): the lever's layout, the car's gear (car +0x624), what the driver reaches for
// (0x40CA58) and whether the race's countdown holds the cars (0x34C210, the context's +0x120).
struct GearInput {
    GearLayout layout;
    int gear = 0;
    ShiftReach reach;
    bool start_hold = false;
};

// What the car does at a step: its place and turn (of the program's 0x344858 pitch, 0x3448B8 yaw, -0x344888 roll), its roll
// (0x344888) and, for the first step, its velocity (course space).
struct DriverMotion {
    V3 pos;
    Quat turn;
    F roll;
    V3 velocity;
    // For the gaze (driver_gaze.h): the front wheels' mean angle; the computer driver's aim (course space; none: no aim);
    // a co-driver.
    F front_wheels{};
    std::optional<V3> aim;
    bool co_driver = false;
    // The gear change's (none: the lever and the left foot stay).
    const GearInput* gear = nullptr;
};

struct DriverState {
    DriverRecord before, after; // the step before's (+0x260), the last one's (+0x160)
    // The filter block (+0x40): the hands' angles on the wheel (+0x90), their ways (+0x88), how long they have been close
    // together or crossed (+0x74), the wheel's last four rates (+0xAC).
    F hand[2], way[2], idle;
    std::array<F, 4> rates{};
    // The seat (+0x3C), its velocity (+0x48) and acceleration (+0x54) at the last step; the body's springs: the spine's and
    // the neck's turns (+0x00 .. +0x10, +0xD0 .. +0xD8), how far the seat's push is left over (+0x64, +0x68), the body's
    // height (+0x60).
    V3 seat, seat_rate, seat_accel;
    F lean_neck_rate, nod_neck_rate, lean_rate, nod_rate, heave_rate, height, lean_left, nod_left, tilt, tilt_rate, head_rate;
    bool first = true; // no step yet (the ints' +0x28)
    u8 state[2] = {0, 0}, was[2] = {0, 0}; // the hands' states (+0x30): 0 hold, 1 at the end of the turn, 2 / 3 going ...
    DriverGaze gaze; // where the driver looks (+0x58 .. +0x60)
    // The gear change: the hand's rate (+0x1C), the lever's (+0x20, +0x24) and the grip's (+0x28, +0x2C), the rest before the
    // hand goes back to the wheel (+0x6C), the lever's wait at its gate (+0x70), the wheel's turn's hold off the reach (+0x78:
    // 1 with the wheel more than 18 degrees round, less a quarter a second); the gear the hand has put the lever in (the ints'
    // +0x00), the car's at the last step (+0x04), the phase (+0x18: 0 on the wheel, 1 / 2 going for the lever for a reach or a
    // shift, 3 / 4 on the way, 5 at the lever, 6 moving it, 7 / 8 / 9 back to the wheel).
    F hand_rate, lever_rate[2], grip_rate[2], rest, gate_wait, busy;
    int held_gear = 0, seen_gear = 0, phase = 0;
    F forward; // the seat's velocity ahead in the car's frame at the last step (the context's +0xF8)

    // At rest in the cockpit (0x402320), the lever at the gear's place.
    void reset(const Cockpit& c, const GearLayout& layout = {}, int gear = 0);
    // A step of `dt` seconds; `follow`: the steering command's follower (F +0x664).
    void step(const Cockpit& c, F follow, F dt, const HandSpeeds& speeds, const DriverMotion* motion = nullptr);

private:
    void body_step(const Cockpit& c, const DriverMotion& m, F dt);
    void hand_step(int h, bool regrip, F turned, F dt, F rate, const HandSpeeds& speeds);
    void gear_step(const Cockpit& c, const GearInput& in, F dt, const HandSpeeds& speeds);
    static F rim_hold(int phase, int state, F place, F other, F hold, F dt);
};

} // namespace gt4r::race::posture
