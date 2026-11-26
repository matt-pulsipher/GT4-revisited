#pragma once

// The tires a race puts on its entries before their cars are built (the race object's virtual 92: RaceArcade 0x3873xx,
// RaceLanBattle 0x338348, RaceSplitBattle 0x3306xx, RaceFreeRun 0x3BA5xx; a Gran Turismo mode event has none): on a dirt
// course (COURSE +0x10 2) every entry takes tire kind 11, on snow (3) 12, else the entry with its byte +0x165 set the kinds
// at +0x160 (front) and +0x161 (rear), which the arcade's menus fill with the tires chosen (0x111B50). The part is the
// car's fitting part of that kind (its CARS file); a car without one keeps its tires.

#include "gt4r/sim/pit_tires.h"

namespace gt4r::sim {

// `chosen`: the kind chosen (0 .. 12), -1: the entry's own tires. Answers whether the equipment changed.
bool fit_race_tires(db::SpecDb& db, const FileReader& read, Equipment& eq, int course_kind, int chosen);
// The kinds of the entry's +0x160 (front) and +0x161 (rear) with its flag +0x165 set (the Quick Tune's: sim/quick_tune.h):
// nothing changes when they are the car's own kinds; else each axle takes its kind's fitting part.
bool fit_race_tires(db::SpecDb& db, const FileReader& read, Equipment& eq, int course_kind, int front, int rear);

} // namespace gt4r::sim
