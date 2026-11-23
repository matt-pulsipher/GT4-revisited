#pragma once

// The seam between what moves the cars and what shows them. A Simulation steps every car of a
// session from the drivers' inputs; the race view, the sounds and the race display read the
// cars' states and nothing else of it. Positions and directions are in course space, as the
// course's files have them (metres).

#include "gt4r/formats/course/runway.h"

#include <cmath>
#include <span>

namespace gt4r::race {

using course::Vec3;

// What a driver asks of a car in a step: the player's pad, or a computer driver's decision.
struct CarInput {
    float steer = 0;    // -1 full left .. +1 full right
    float throttle = 0; // 0..1
    float brake = 0;    // 0..1
    bool handbrake = false;
    bool shift_up = false, shift_down = false; // held this step (the simulation finds the edges)
    bool reverse = false;
    float steer_angle = NAN; // a computer driver's road-wheel steering command (radians, + left), used instead of steer when set
};

struct WheelState {
    Vec3 center;          // the wheel's center, course space
    float steer = 0;      // radians about the car's up axis, + to the right
    float spin = 0;       // radians rolled so far (forward +), for the turning wheel
    float spin_rate = 0;  // radians per second
    float travel = 0;     // suspension travel from rest, metres (+ compressed)
    float slip = 0;       // 0 gripping .. 1 sliding (smoke, squeal)
    bool on_ground = true;
    u8 surface = 0;       // the flags byte of the road triangle under it (Runway::Triangle::kind)
    u8 material = 0;      // the road's material under it (the triangle's byte +6, bits 0-4)
};

struct CarState {
    Vec3 position;                // the body's reference point
    Vec3 right{1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1}; // the body's axes, unit length
    Vec3 velocity;                // metres per second
    float speed = 0;              // along `forward`, metres per second (negative backing up)
    float engine_rpm = 0;
    int gear = 1;                 // -1 reverse, 0 neutral, 1.. forward gears
    float throttle = 0, brake = 0, steer = 0; // as applied this step (after the aids)
    bool brake_lamps = false, reverse_lamps = false;
    WheelState wheels[4];         // front left, front right, rear left, rear right
    float course_v = 0;           // metres along the course (Runway::Checkpoint::v)
    int lap = 0;                  // laps completed; -1 for a car that starts behind the line, until
                                  // it first crosses it
};

// One session's cars. Car 0.. in grid order; `inputs` has one entry per car (a car whose
// entry is missing coasts).
class Simulation {
public:
    virtual ~Simulation() = default;
    virtual void step(std::span<const CarInput> inputs, float dt) = 0;
    virtual std::span<const CarState> cars() const = 0;
};

} // namespace gt4r::race
