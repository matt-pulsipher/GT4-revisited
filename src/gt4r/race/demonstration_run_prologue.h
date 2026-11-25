#pragma once

// One of Gran Turismo 4 Prologue's recorded runs (formats/replay/demonstration_prologue.h: the lessons' demonstrations, made
// on the console) played without a picture, as demonstration_run.h plays Gran Turismo 4's: the lesson set up as the race
// screen sets it up, the player's car given the recording, until the run ends or the recording does.

#include "gt4r/race/demonstration_run.h"

namespace gt4r::race::prologue {

struct Options : DemonstrationOptions {
    bool judge = true; // (off: nothing fails the run)
    int shift = 0;     // the recording's step given at the race's first
    int hold = -1; // the start's hold (seconds; -1: the race's own)
};

// Plays replay/JP/<label>/<label> ("l0p0001").
DemonstrationRun run_demonstration(Disc& disc, db::SpecDb& db, const std::string& label, const Options& options = {});

} // namespace gt4r::race::prologue
