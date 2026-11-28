#pragma once

// What a race sends back to the player's devices: the pad's motors (Gran Turismo 4's own level, 0x36CEE8, or the shared
// engine's Modern rumble fed from Gran Turismo 4's step), a DualSense's or Xbox pad's triggers, and a steering wheel's force
// (0x353EE8). The program sends a car's pad block once a step, after the step (0x354A78 in 0x34DB30), and makes the stopping
// one each frame before it (0x43D680): a paused race, a replay, a menu and the end of a run send nothing. Here the same: a
// frame without a step's feeling stops everything. docs/specs/input.md.

#include "gt4r/input/key_config.h"
#include "input/force_feedback.h"
#include "input/input_manager.h"
#include "race/controls.h"

namespace gt4r::sim {
class Session;
}

namespace gt4r::input {

// A step of a car as the feedback reads it, in plain values (all from the step's own record, sim::Session).
struct CarFeel {
    bool pad_output = false;   // the pad drives it (control source 0, driver kind 0): else nothing is sent
    float speed = 0;           // m/s
    float throttle = 0, brake = 0; // the pedals as the input phase left them, 0 .. 1
    u8 rumble = 0;             // the pad block's +0x01: the large motor (the vibration's level 0 .. 127)
    u8 small = 0;              // the pad block's +0x00: the small motor (bit 0 on; Gran Turismo 4's vibration leaves it 0)
    float steer_force = 0;     // the wheel's force -1 .. 1, positive clockwise (the pad block's +0x02: 0x80 at rest, less below)
    struct Wheel {
        bool grounded = false;
        float slip_ratio = 0;  // positive braking
        float slide = 0;       // the sliding measure (wheel +0xB4: the skid sound's, the wear's)
        bool kerb = false;     // on a kerb strip (the surfaces the program's vibration takes for one: 1, 10, 11, 21)
    };
    std::array<Wheel, 4> wheels;
    bool offroad = false;      // a wheel on a soft material (F +0x62C)
    float impact = 0;          // the step's strongest hit as a change of speed, m/s
    float impulse = 0;         // and as an impulse, N s
    float abs_level = 0;       // the ABS at work, 0 .. 1 (a wheel without it: how far past its best slip it is)
    float traction = 0;        // the driven wheels slipping past their best under throttle, or the TCS cutting, 0 .. 0.6
    float understeer = 0, oversteer = 0; // the aids' indicators (F +0x470 / +0x478), 0 .. 1
};
CarFeel car_feel(const sim::Session& session, size_t car);

// What the player chose.
struct FeedbackOptions {
    bool vibration = true; // the game's option (the DualShock 2's "ON")
    bool wheel = true;     // the wheel's "ON"
    // Read from a port's settings: the pad's and the wheel's of `wheel` (1 .. 3).
    static FeedbackOptions from(const PortConfig& port, u8 wheel);
};

class RaceFeedback {
public:
    // After each simulation step.
    void step(const CarFeel& feel, float dt);
    // Once a frame of `dt` seconds: sends what the steps since the last frame felt. Without a step for a tenth of a second
    // (a frame may be shorter than a step, or much longer) everything stops.
    void frame(gt3r::InputManager& input, int player, const FeedbackOptions& options, double dt);
    // At once: the motors still, the triggers free, no force on the wheel. A race pausing, ending, replaying, losing the
    // window's focus, its screen going.
    void stop(gt3r::InputManager& input, int player);

    // The motors: "Gran Turismo 4 original" the pad block as the program's port packs it for the pad (0x552200: bit 0 of
    // +0x00 the small motor, on or off; +0x01 the large motor's byte as it is), else the shared engine's Modern rumble.
    gt3r::PadRumble rumble(bool original, bool dualsense) const;
    gt3r::ForceFeedbackState force(const FeedbackOptions& options) const;
    bool feeling() const { return age_ <= kHold; }

private:
    gt3r::race::RumbleModel model_;
    CarFeel feel_;
    float jolt_ = 0;   // the steps' strongest impulse since the last frame
    static constexpr double kHold = 0.1;
    double age_ = 1e9; // seconds since the last step
    bool sent_ = false;
};

} // namespace gt4r::input
