#pragma once

// Watching a replay: the pad's controls (RaceInput's replay words, 0x43D288, as the camera manager reads them, 0x3FB1E0), the
// playback mode (the manager's style, screen +0x138), the car-mounted camera's places (CameraRideOn 0x37D088) and the held zoom.
// docs/formats/camera.md "Watching a replay".

namespace gt4r::race {

// The playback mode: with the race's display, without it, or Dive (the director's cameras, cut to the music).
enum class ReplayStyle { Display, Plain, Dive };

// The pad this step: `held` as they are, the others as they go down.
struct ReplayPad {
    bool up = false, down = false, cross = false, square = false, circle = false, l1 = false, r1 = false, l2 = false, r2 = false, l3 = false;
    bool left_held = false, right_held = false, triangle_held = false;
};

class ReplayView {
public:
    // A replay's start: the style by the options (0x372970: Dive by "replay_mode", else by "replay_display_enable").
    void begin(ReplayStyle style, bool ride_on = false);

    struct Events {
        int select_car = 0;      // -1 the car ahead in the race's order, +1 the one behind
        bool style = false;      // the playback mode changed
        bool view = false;       // between the course's cameras and the car's, or another place on the car: a cut
        bool music = false;      // the music on or off
        bool speed = false;      // the speed x 1 or x 4
        bool director = false;   // (Dive) a cut asked for
    };
    Events step(const ReplayPad& pad);

    ReplayStyle style() const { return style_; }
    // The car's own cameras (the manager's mode 2); else the course's (mode 1), or Dive's.
    bool ride_on() const { return ride_on_ && style_ != ReplayStyle::Dive; }
    int mount() const;
    bool look_back() const;
    float zoom() const; // 1 .. 4, the course's cameras
    int speed() const { return fast_ ? 4 : 1; }
    bool fast() const { return fast_; }

private:
    ReplayStyle style_ = ReplayStyle::Plain;
    bool ride_on_ = false, fast_ = false;
    int place_ = 0, side_ = 0, wheel_ = 0; // of the table 0x621200; held left / right; L2 / R2
    bool back_ = false;
    float zoom_ = 0, zoom_rate_ = 0; // (0x3852A0: 0 .. 20)
};

} // namespace gt4r::race
