#pragma once

// The seam between what moves the cars and what shows them. A Simulation steps every car of a
// session from the drivers' inputs; the race view, the sounds and the race display read the
// cars' states and nothing else of it. Positions and directions are in course space, as the
// course's files have them (metres).

#include "gt4r/formats/course/runway.h"

#include <array>
#include <cmath>
#include <span>

namespace gt4r::race {

using course::Vec3;

// A recorded pad frame: Gran Turismo 4's own 32 bytes as the recorder (0x345D70) hands them to the input phase (+0 flags:
// bit 0 the steering analog, 1 the throttle, 2 the brake, 3 the back axis, 4 the third axis; +1 the controller's kind (0
// digital, 2, 3 a DualShock's stick, 4 and 6 wheels); +2 s8 the shift; +3 the count of button bytes; +4 s16 the steering
// (+ left, +-4096); +7 u8; +8 the throttle, +0xA the brake, +0xC the back axis, +0xE the third axis; +0x10 .. the button
// bytes).
struct PadRecord {
    std::array<u8, 32> bytes{};
};

// What a driver asks of a car in a step: the player's pad, or a computer driver's decision.
struct CarInput {
    float steer = 0;    // -1 full left .. +1 full right
    float throttle = 0; // 0..1
    float brake = 0;    // 0..1
    bool handbrake = false;
    bool shift_up = false, shift_down = false; // held this step (the simulation finds the edges)
    bool reverse = false;
    float steer_angle = NAN; // a computer driver's road-wheel steering command (radians, + left), used instead of steer when set
    // A computer driver's outputs (0x464F38): throttle, brake, steering command and reverse written into the controls as
    // they are, the pad's path not run; shift_lock holds the gearbox that many steps.
    bool computer = false;
    u8 shift_lock = 0;
    const PadRecord* pad = nullptr; // a recorded pad frame (a demonstration or a replay): the input phase reads it instead of building one from the fields above
    u8 control = 2; // a computer's car: 2, the B-spec car 1 (F +0x466)
};

struct WheelState {
    Vec3 center;          // the wheel's center, course space
    float steer = 0;      // radians about the car's up axis, + to the right
    float spin = 0;       // radians rolled so far (forward +), for the turning wheel
    float spin_rate = 0;  // radians per second
    float travel = 0;     // suspension travel from rest, metres (+ compressed)
    float slip = 0;       // 0 gripping .. 1 sliding (smoke, squeal)
    float brake_heat = 0; // the brake's heat (the simulation's wheel +0xA4): its disc glows by its square
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
    bool clutch_closed = true;    // the clutch is locked (F +0x522 = 1): the race display's gear digit is dimmed while it is not
    float throttle = 0, brake = 0, steer = 0; // as applied this step (after the aids)
    bool brake_lamps = false, reverse_lamps = false;
    WheelState wheels[4];         // front left, front right, rear left, rear right
    float course_v = 0;           // metres along the course (Runway::Checkpoint::v)
    int lap = 0;                  // laps completed; -1 for a car that starts behind the line, until
                                  // it first crosses it
    // This step's contact: its strength, what was hit (0 none, 1 a wall, 2 another car) and that
    // car's index; a wheel back on the ground. The strength is the collision step's own measure, the
    // one the game's hit sound and rumble take (sim::Session: the body's strength, 0 for none): the
    // step's summed impulses |j| (N s) over 100,000 against a wall and over 15,000 between cars, at
    // most 1 (not a force in newtons, nor a closing speed). `landed` carries no strength: the car's
    // downward speed is in `velocity`.
    float contact = 0;
    u8 contact_kind = 0;
    int contact_car = -1;
    bool landed = false;
    bool rolling = false; // held in its rolling start (F + 0x467 = 1) until the simulation lets it go (release_start)
};

// One session's cars. Car 0.. in grid order; `inputs` has one entry per car (a car whose
// entry is missing coasts).
class Simulation {
public:
    virtual ~Simulation() = default;
    virtual void step(std::span<const CarInput> inputs, float dt) = 0;
    virtual std::span<const CarState> cars() const = 0;
    // A car held in its rolling start let go (the single-player conductor's virtual 20 (+0xA0): 0x345228).
    virtual void release_start(int car) {}
};

} // namespace gt4r::race
