#pragma once

// The hands of the driver's posture (the program's HandleSolvers, 0x415410-0x416B54): where a hand is and which way its
// palm and fingers face, on the steering wheel, on its way to the gear lever, on fixed grips, or free.

#include "gt4r/race/posture_solver.h"

namespace gt4r::race::posture {

struct HandSolve {
    int side = 0;       // +0x00: 0 left, 1 right
    V3 pos{};           // +0x10
    V3 palm{};          // +0x20
    V3 finger{};        // +0x30
    F grip_side{};      // +0x40
    F hand_turn{};      // +0x44
    F wrist{};          // +0x48
    V3 offset{};        // +0x50: from the gear lever
    F weight[2]{};      // +0x60
    F out{}, back{};    // +0x68, +0x6C: off the rim, outwards and away
    F wrist_add{};      // +0x70
    int grip_kind = 0;  // +0x74
    int raised = 0;     // what the solver answers: the hand is past the wheel's side
};

// Whether the hand is the one that changes gear.
bool shifts(const Places& p, int side);
void easy_hand(HandSolve& h, const Places& p);  // 0x4154A0: the wheel's side
void fixed_hand(HandSolve& h, const Places& p); // 0x415510: handlebars
void free_hand(HandSolve& h, const Places& p);  // 0x415648: beside the body
// A hand on the wheel between two angles (0 the top), or between the wheel and the lever (0x415D38).
int single_hand(HandSolve& h, F from, F to, const Places& p, F blend, F phase);

} // namespace gt4r::race::posture
