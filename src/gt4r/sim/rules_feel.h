#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): what phase 19 makes for the player and the views: the pad's
// vibration (0x36CEE8), the view's follower of the heading and pitch (0x345658) and the body's sway against the ground
// (0x34A668, 0x34A788). Their state lives in the conductor, per car (its index, F + 0x440): cond + 0xCA58 + 0x10 k (the
// pad) and cond + 0xCAB8 + 0x30 k (the views).

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"

namespace gt4r::sim {
class Program;
}

namespace gt4r::sim::rules {

struct WheelLayout;

// A car's block at cond + 0xCAB8 + 0x30 k (0x3455F0 sets it at the car's set-up: the lags at the heading and pitch, the
// rest 0). +0x1C .. +0x24 are the collision step's results (§7).
struct ViewState {
    F heading, heading_rate, heading_acc; // +0x00 / +0x04 / +0x08 a follower of the heading (its acceleration kept)
    F pitch, pitch_rate;                  // +0x0C / +0x10 a follower of the pitch
    F sway_pitch_rate, sway_pitch;        // +0x14 / +0x18 the body's pitch against the ground's, followed (0x34A668)
    F sway_roll_rate, sway_roll;          // +0x28 / +0x2C the same for the roll (0x34A788)
};
constexpr u32 kViewBlock = 0xCAB8, kViewSize = 0x30, kPadBlock = 0xCA58, kPadSize = 0x10;

// What these routines read of the record that the engine's mirror has no member for.
struct FeelExtra {
    F blend; // car + 0x554: the step's blend between the last step's attitude and this one's (0x344858 ..; 0 in the races seen)
    ViewState view;          // (the conductor's, cond + 0xCAB8 + 0x30 k)
    std::array<u8, 2> pad{}; // (the conductor's, cond + 0xCA58 + 0x10 k)
    std::array<F, 4> anim;   // car + 0x778: the driver's animation's slots (2 the throttle, 3 the brake followed: 0x344040)
};
template <class X, class V> void feel_fields(X& x, V&& v) {
    v("feel.blend", -1, layout::Block::Car, 0x554, x.blend);
    for (int k = 0; k < 4; ++k) v("feel.anim", k, layout::Block::Car, 0x778 + 4 * u32(k), x.anim[size_t(k)]);
}

// What the routines read of the program, as the race's start leaves it.
struct FeelTables {
    F view_heading_gain, view_heading_damp; // 0x69F4C8 / 0x69F4D0 (120, 15)
    u8 sway_pitch_on = 0, sway_roll_on = 0; // 0x6202B0 / 0x6202B5
    // 0x6202C8 .. (0x34A4B8 from the bytes 0x6202B1 .. 0x6202B9): per sway its limit (x 0.1 degree), gain (x 0.1), spring and
    // damping.
    std::array<F, 4> sway_pitch{}, sway_roll{};
    std::array<F, 32> cue{}; // 0x843630 ..
};
// 0x843630 .. (32 words) as 0x34F6A0 makes them at the race's start from the cue bytes 0x622690 .. (after 0x3FAA10's fix-ups):
// the engine sound's level scales at +0x64 .. +0x70, the others the steering force's and the cues' ❓.
constexpr u32 kCueTable = 0x843630, kCueBytes = 0x622690;
std::array<F, 32> cue_table(const u8* bytes);  // from the 0x30 fixed bytes at kCueBytes
FeelTables feel_tables(const Program& program);
constexpr u32 kSwayBytes = 0x6202B0;
// From the bytes 0x6202B0 .. 0x6202B9.
std::array<F, 8> sway_constants(const u8* bytes);

// 0x3455F0: the view block at a car's set-up.
void view_start(const ex::Car& c, ViewState& v);
// 0x345658: the heading's follower (a = gain x (heading - lag) - damping x rate, its rate moved by the mean of this and
// the last acceleration) and the pitch's (20, 10); F + 0x5FC / + 0x600 the angles less their lags.
void view_follow(ex::Car& c, ViewState& v, const FeelTables& t);
// 0x34A668 / 0x34A788: the body's pitch (roll) against the ground's at the step's blend (`blend`, car + 0x554), held within
// the limit, followed by a spring.
void sway(const ex::Car& c, ViewState& v, const FeelTables& t, F blend);

// 0x350080 (phase 19): the engine sound's levels (F + 0x604 the engine's: the load's distance from the friction's share plus
// that share, half with the clutch not locked; + 0x605 the intake's: 1 - (1 - the load over the friction's rest)^16 x 0.6 +
// 0.4; each x 127 x its scale, averaged with the last step's; a hybrid's halved and both 0 while its engine is below idle;
// + 0x606 the turbo's spool and F + 0x504 the blow-off each times its scale; all 0 for a car not drawn as one in the race.
void engine_sound(ex::Car& c, const FeelTables& t, u8 gearbox_kind, int draw_mode);

// 0x36CEE8: the pad's vibration into `pad` (cond + 0xCA58 + 0x10 k: +0x01 the level 0 .. 127; a car a computer drives both
// bytes 0): the strongest of the driving force at the wheels ahead of the center of gravity (x their mean squared slip
// ratio + 0.1), the braking force there, a wall's hit (F + 0x610) and the bumps (the dampers' forces) and kerb strips under
// the wheels ahead (surfaces 1, 10, 11, 21: the load times the speed) while the start does not hold the car.
void pad_rumble(const ex::Car& c, const WheelLayout& l, std::array<u8, 2>& pad);

} // namespace gt4r::sim::rules
