#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): tire wear, the pit flags, the flaps and
// brake heat.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim {
class Program;
}

namespace gt4r::sim::rules {

// The race's wear rules (the conductor + 4; 0x6C bytes): two kinds of wear, each with its own switch,
// and their grip curves. The tread wear (wheel +0xAC) takes the grip from 1 down through three knees;
// the second wear (wheel +0xA8) through one. Below 0 each has a negative branch of its own.
struct WearRules {
    std::array<u8, 0x6C> raw{}; // (the block as it is)
    F tread_neg;            // +0x00 the tread wear's least (negative) value
    F tread_neg_loss;       // +0x04 the grip lost there
    F tread_knee1;          // +0x08 the first knee
    F tread_loss1;          // +0x0C the grip lost at it
    F tread_knee2;          // +0x10
    F tread_loss2;          // +0x14
    F tread_max;            // +0x18 the tread wear's greatest value (the wear is held there)
    F tread_max_loss;       // +0x1C
    F second_neg;           // +0x50
    F second_neg_loss;      // +0x54
    F second_knee;          // +0x58
    F second_loss;          // +0x5C
    F second_max;           // +0x60
    F second_max_loss;      // +0x64
    u8 tread_on = 0;        // +0x68 (0x367D20)
    u8 second_on = 0;       // +0x69 (0x367D30)
};
// From the block's 0x6C bytes.
WearRules wear_rules(const u8* block);


// What these routines keep that the engine's record has no member for.
struct WearExtra {
    std::array<F, 4> second{}; // wheel +0xA8: the second wear
    std::array<u8, 4> hud{};   // wheel +0xB0: the tread wear's display (0 .. 255)
    u8 category = 0;           // P + 0x32: the car's category (4: no wear)
};
template <class X, class V> void wear_fields(X& x, V&& v) {
    for (int w = 0; w < 4; ++w) {
        v("w.second_wear", w, layout::Block::Car, layout::kWheel0 + layout::kWheelSize * u32(w) + 0xA8, x.second[size_t(w)]);
        v("w.wear_hud", w, layout::Block::Car, layout::kWheel0 + layout::kWheelSize * u32(w) + 0xB0, x.hud[size_t(w)]);
    }
    v("wear.category", -1, layout::Block::Params, 0x32, x.category);
}

// 0x367C90 (the conductor's configuration, 0x34CA20): the block cleared (0x3678A8); the second wear's rules from the
// program's table 0x620B60 when `second_on` (0x367AF8: the conductor's virtual +0x58, 1 for a single race, off on a loose
// course or a course whose row byte (conductor + 0x10000 - 0x3420) is not 1); the tread wear's from the rule block's bytes
// +0x2C .. +0x34 when they make a curve (0x367918: its knees x 10,000, its losses in percent) and the static block's
// +0xBE / +0xC2 / +0xC3. Both off for a rule block of zeros (the races seen).
std::array<u8, 0x6C> wear_rules_block(const u8* rule_block, bool second_on, const Program& program);
// 0x362E58: a wheel's new tire (the start, a pit stop): the grip 1, the wears, levels, brake heat cleared; with the tread wear
// on it starts at the rules' +0x00 and the grip loses +0x04; with the second wear on that starts at +0x50 and the grip loses
// +0x54 more; a category 4 car none of it.
void new_tire(ex::Car& c, WearExtra& x, int wheel, const WearRules& k);

// 0x356EF0: each wheel's grip from its wear and its HUD level, the grip never below its tire's floor;
// false (nothing done) with both wears off or on the pit lane.
bool wear_grip(ex::Car& c, WearExtra& x, const TireExtra& t, const WheelLayout& l, const WearRules& k);

// 0x579330: the program's shared random stream (its state at 0x874D50): state x 17 + 17, the value the state xor the state with
// its halves swapped. 0x578598 / 0x579398: that as a float in 0 .. 1 (built from its bits) times `scale`.
u32 random_next(u32& state);
F random_float(u32& state, F scale);
constexpr u32 kRandomState = 0x874D50;

// 0x35A678 (phase 22): each wheel's flap (wheel +0xC0, its rate +0xC4): a pendulum on a spring of 500 and a damping of 10,
// pushed toward a random target in -1 .. 1 whose reach grows with the hub's speed (a tenth of it, at most 2, less 1), within
// -1 .. 1.
void flaps(ex::Car& c, const WheelLayout& l, u32& random_state);

// 0x3F38D8 (phase 11): each wheel's brake heat (wheel +0xA4): heating by the axle's brake torque times its
// tire's radius, the brake pressure and the rim's speed (none while the brake pedal is up), cooling by the speed
// and the heat; the constants k (0x845A00: heating, cooling rate, cooling speed offset, heat offset) as the
// engine's BrakeHeatConsts.
void brake_heat(ex::Car& c, const WheelLayout& l, const ex::BrakeHeatConsts& k, const std::array<F, 2>& radius);
constexpr u32 kBrakeHeatConsts = 0x845A00;

} // namespace gt4r::sim::rules
