#include "gt4r/race/display_mode_prologue.h"

namespace gt4r::race {

using gt3r::race::HudMode;
using gt3r::race::HudPlay;
using gt3r::race::HudRun;

HudMode prologue_display_mode(HudRun run, HudPlay play) {
    // The program's race classes: the arcade's races (and its demonstration) RACE_MODE_SINGLE, the school's tests and lessons
    // RACE_MODE_LICENSE, the machine tests theirs. It has no free run, time trial, free practice or split-screen race.
    HudMode mode = HudMode::Race;
    switch (run) {
    case HudRun::License: mode = HudMode::License; break;
    case HudRun::MachineTime: mode = HudMode::MachineTime; break;
    case HudRun::MachineSpeed: mode = HudMode::MachineSpeed; break;
    default: break;
    }
    if (play == HudPlay::Replay) return HudMode::Replay;
    if (play == HudPlay::Demo) return HudMode::Demo;
    return mode;
}

} // namespace gt4r::race
