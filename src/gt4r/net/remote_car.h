#pragma once

// Another player's car "now", from the states its owner's machine sends (docs/specs/net.md "Race").
//
// The prediction is the car alone on a one-car session of our course (sim::Session::solo). It is kept in two parts: the car
// as its owner last sent it, reached by running the owner's own frames from the state before (every state repeats the last
// three, so a lost datagram costs none) and then set from the state; and from there, with the owner's newest frame, up to
// our race's clock. What the state does not carry (the tires' and brakes' heat, wear, fuel, the turbo, the pedals' and the
// steering's filters, a hybrid's battery) so follows the owner's car step for step.
//
// When a state arrives, the jump between what was shown and the new prediction is blended out over about 0.1 s. Collisions
// use the prediction itself.

#include "gt4r/net/car_state.h"

#include <array>
#include <deque>
#include <memory>

namespace gt4r::net {

using course::Vec3;
using gt3r::s64;

class RemoteCar {
public:
    static constexpr int kMaxAhead = 24;          // steps (0.4 s) ahead of the newest state; older data is held, not flung on
    static constexpr int kMaxReplay = 30;         // steps of the owner's run between two states made up for
    static constexpr float kBlend = 0.1f;         // s, the time constant of a shown correction
    static constexpr float kSnapDistance = 8.0f;  // m: larger jumps are not blended

    void attach(std::unique_ptr<sim::Session> solo);
    bool attached() const { return car_ != nullptr; }
    // Dead reckoning instead of the car's own step: for comparison.
    void set_physics(bool on) { physics_ = on; }
    // What its race knows of the car's surroundings, for its next steps: the slipstream (F + 0x528) and the driving line's heading
    // and mode (the active steering's), each from the race's last step.
    void set_surroundings(gt3r::ee::F slipstream, std::optional<gt3r::ee::F> line_heading, u8 line_mode);

    // A state arrived; `clock`: our race's clock (units of 1/3000 s) at the end of its last step.
    void receive(const CarSnapshot& s, s64 clock);
    bool has() const { return have_; }
    const CarSnapshot& latest() const { return newest_; }
    u32 states() const { return states_; }

    // The car at the end of the coming step (`clock`), run up to it; nullptr before its first state.
    const sim::Session* predict(s64 clock);
    sim::Session::Shown shown(s64 clock) const;
    // How far the prediction was from where the owner's state said the car was, and how long after the state it ran from (s).
    float last_error() const { return last_error_; }
    double last_horizon() const { return last_horizon_; }

private:
    static constexpr size_t kPending = 16;
    void apply(const CarSnapshot& s, s64 clock);
    void run_to(s64 clock);
    void step(const PadInput& p, const CarEntry& e, const CarMotion& m, bool owners, s64 from);
    struct Place {
        s64 clock = -1 << 30;
        Vec3 pos{};
        float heading = 0;
    };
    Place place(s64 clock) const;

    std::unique_ptr<sim::Session> car_;
    std::unique_ptr<sim::Session::Memory> base_; // the car at the newest applied state
    bool physics_ = true;
    bool have_ = false;
    CarSnapshot snap_, newest_;
    std::deque<CarSnapshot> pending_; // states ahead of our race's clock, oldest first
    PadInput held_{};                 // the newest frame
    s64 base_clock_ = 0, clock_ = 0;  // the state's, and the one the prediction has reached
    u32 states_ = 0;
    gt3r::ee::F slip_{};
    std::optional<gt3r::ee::F> line_;
    u8 line_mode_ = 0;
    Vec3 off_{};                      // shown - predicted at off_clock_
    float off_yaw_ = 0;
    s64 off_clock_ = 0;
    std::array<Place, 64> trail_{};   // the predictions, for last_error
    size_t ntrail_ = 0;
    float last_error_ = 0;
    double last_horizon_ = 0;
};

} // namespace gt4r::net
