#include "gt4r/race/replay_view.h"

#include <algorithm>

namespace gt4r::race {

namespace {

// The car's places (0x621200 ahead, 0x621228 back: a mount and whether the view turns back), then those held by Right and Left
// (0x621250, 0x621258) and put on by R2 and L2 (0x621260, 0x621268).
struct Place {
    int mount;
    bool back;
};
constexpr Place kPlaces[2][5] = {{{0, false}, {1, false}, {20, false}, {7, false}, {5, false}}, {{0, true}, {1, true}, {20, true}, {8, false}, {9, false}}};
constexpr int kRight = 11, kLeft = 10, kR2 = 4, kL2 = 3;

} // namespace

void ReplayView::begin(ReplayStyle style, bool ride_on) {
    *this = {};
    style_ = style, ride_on_ = ride_on;
}

ReplayView::Events ReplayView::step(const ReplayPad& pad) {
    Events e;
    e.music = pad.circle && style_ != ReplayStyle::Dive; // (Dive's cuts follow the music: 0x33C658)
    if (pad.l3) fast_ = !fast_, e.speed = true;
    if (style_ == ReplayStyle::Dive) {
        e.director = pad.cross;
    } else if (pad.cross) {
        ride_on_ = !ride_on_;
        e.view = true;
    }
    if (pad.square) {
        style_ = style_ == ReplayStyle::Display ? ReplayStyle::Plain : style_ == ReplayStyle::Plain ? ReplayStyle::Dive : ReplayStyle::Display;
        e.style = true;
        e.view = e.view || style_ == ReplayStyle::Dive || style_ == ReplayStyle::Display;
    }
    if (style_ != ReplayStyle::Dive && pad.up != pad.down) e.select_car = pad.up ? -1 : 1;
    // The zoom (0x3852A0): toward 20 while held, by a spring; let go, 0.8 of it a step.
    const float want = !ride_on() && style_ != ReplayStyle::Dive && pad.triangle_held ? 20.0f : 0.0f;
    if (zoom_ < want) {
        zoom_rate_ = std::clamp(zoom_rate_ + ((want - zoom_) * 40 - 10 * zoom_rate_) / 60, -900.0f, 900.0f);
        zoom_ += zoom_rate_ / 60;
    } else {
        zoom_ = std::max(zoom_ * 0.8f, want);
    }
    if (ride_on() && !e.view) {
        // CameraRideOn 0x37D088: nothing but the held side while it is held; L2 / R2 put a camera on and off; R1 the next
        // place, L1 ahead or back, either of them from L2 / R2's back to the places.
        const int side = int(pad.right_held) - int(pad.left_held), wheel = int(pad.r2) - int(pad.l2);
        if (side_ == 0) {
            if (wheel_ != 0) {
                if (wheel != 0) wheel_ = wheel == wheel_ ? 0 : wheel, e.view = true;
                else if (pad.r1 || pad.l1) wheel_ = 0, e.view = true;
            } else if (wheel != 0) {
                wheel_ = wheel, e.view = true;
            } else if (pad.r1) {
                place_ = (place_ + 1) % 5, e.view = true;
            } else if (pad.l1) {
                back_ = !back_, e.view = true;
            }
        }
        if (side != side_) side_ = side, e.view = true;
    }
    return e;
}

int ReplayView::mount() const {
    if (side_ != 0) return side_ > 0 ? kRight : kLeft;
    if (wheel_ != 0) return wheel_ > 0 ? kR2 : kL2;
    return kPlaces[back_ ? 1 : 0][place_].mount;
}

bool ReplayView::look_back() const { return side_ == 0 && wheel_ == 0 && kPlaces[back_ ? 1 : 0][place_].back; }

float ReplayView::zoom() const { return std::clamp(zoom_ / 20 * 3 + 1, 1.0f, 4.0f); }

} // namespace gt4r::race
