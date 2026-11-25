#pragma once

// A race's rules of hits and speed caps as the race's copy of its RACE row makes them (the rule block: Gran Turismo 4's
// conductor + 0xF818, 0x35E108; Prologue's conductor + 0x10780, 0x3C5978): the hit limits and the penalty (+0x46 ← row +0x75,
// +0x48 ← +0x8B, +0x49 ← +0x8A, +0x4A ← +0x88 in Gran Turismo 4's columns), the penalty's cap (+0x4B, a constant of the copy's
// code) and, in Prologue, a license test's own changes (0x3E1C20, a switch on the test's number row +0x72: lesson 1 caps the
// cars' speed, +0x42; tests 26 .. 35 start the other cars on the move). The constants are read from the program's code.

#include "gt4r/sim/rules_phases.h"

#include <array>

namespace gt4r::sim {

class Program;

struct RaceRules {
    rules::HitRules hits;
    std::array<u8, 2> speed_cap{}; // +0x42 / +0x43 (km/h; 0 none)
    u8 penalty_cap = 0;            // +0x4B
    // A race's rolling start by car (+0x4E + k: km/h, a car without one car 0's; +0x54 + 2 k: metres from the line, below 0
    // behind it): car 0's from the row (+0x42 and +0x14 in Gran Turismo 4's columns), Prologue's tests 26 .. 35's other cars'
    // from 0x3E1C20. All 0: a standing start.
    std::array<u8, 6> start_speed{};
    std::array<s16, 6> start_distance{};
};

// `row`: the RACE row in Gran Turismo 4's columns (151 bytes; shorter: no rules).
RaceRules race_rules(ByteSpan row, const Program& program);

} // namespace gt4r::sim
