#pragma once

// Gran Turismo 4 Prologue's choice of its race display's mode (docs/formats/prologue.md "The race display"): the race
// class's label (RaceArcade RACE_MODE_SINGLE, RaceLicense and RaceTraining RACE_MODE_LICENSE), then RaceDisplay::vf1
// (0x389698) and 0x389BA0 by the play kind: a replay RACE_MODE_REPLAY, a demonstration RACE_MODE_DEMO, the two-player modes
// kept. Unlike Gran Turismo 3's display (the shared engine's race::hud_mode), no mode becomes the endurance one for 10 laps
// or more or a time limit.

#include "race/hud_layout.h"

namespace gt4r::race {

gt3r::race::HudMode prologue_display_mode(gt3r::race::HudRun run, gt3r::race::HudPlay play);

} // namespace gt4r::race
