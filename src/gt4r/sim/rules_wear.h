#pragma once

// Gran Turismo 4's own routines of the race step (rules.h): tire wear, the pit flags, the flaps and
// brake heat.

#include "gt4r/sim/car_layout.h"
#include "gt4r/sim/rules.h"
#include "gt4r/sim/rules_tires.h"

namespace gt4r::sim::rules {

// The race's wear rules (the conductor + 4; 0x6C bytes): two kinds of wear, each with its own switch,
// and their grip curves. The tread wear (wheel +0xAC) takes the grip from 1 down through three knees;
// the second wear (wheel +0xA8) through one. Below 0 each has a negative branch of its own.
struct WearRules {
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
};
template <class X, class V> void wear_fields(X& x, V&& v) {
    for (int w = 0; w < 4; ++w) v("w.second_wear", w, layout::Block::Car, layout::kWheel0 + layout::kWheelSize * u32(w) + 0xA8, x.second[size_t(w)]);
}

// 0x356EF0: each wheel's grip from its wear and its HUD level, the grip never below its tire's floor;
// false (nothing done) with both wears off or on the pit lane.
bool wear_grip(ex::Car& c, WearExtra& x, const TireExtra& t, const WheelLayout& l, const WearRules& k);

} // namespace gt4r::sim::rules
