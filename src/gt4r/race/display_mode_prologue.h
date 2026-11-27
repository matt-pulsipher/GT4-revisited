#pragma once

// Gran Turismo 4 Prologue's choice of its race display's mode (docs/formats/prologue.md "The race display"): the race
// class's label (RaceArcade RACE_MODE_SINGLE, RaceLicense and RaceTraining RACE_MODE_LICENSE), then RaceDisplay::vf1
// (0x389698) and 0x389BA0 by the play kind: a replay RACE_MODE_REPLAY, a demonstration RACE_MODE_DEMO, the two-player modes
// kept. Unlike Gran Turismo 3's display (the shared engine's race::hud_mode), no mode becomes the endurance one for 10 laps
// or more or a time limit.

#include "race/hud_layout.h"

namespace gt4r::race {

gt3r::race::HudMode prologue_display_mode(gt3r::race::HudRun run, gt3r::race::HudPlay play);

// The opacity of the race display's Best and Last lines (RaceDisplay + 0x34C, + 0x424: their faders at + 0x374, + 0x44C).
// They show from the display's making until START; START (RaceDisplay::vf33 0x38CDC8) and each lap of the display's car (the
// lap's time event 0x38B678 through vf35 0x38C988) call vf36 (0x38CA18), which starts both faders (0x392DA0: no delay, a hold
// of 8 s, fades of 0.15 s), and the fader (0x392C78) steps a 60th of a second a race step: the call's own step fades out,
// the next ones fade in while the hold counts down from 8 s (it runs out on the 480th step), then the lines fade out.
// A school test's display (RaceLicenseDisplay::vf36 0x386570) keeps them shown. `steps`: race steps since the call (< 0:
// none yet); `from`: their opacity when it came.
float prologue_times_opacity(int steps, float from = 1);

} // namespace gt4r::race
