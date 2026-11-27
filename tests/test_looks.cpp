// What players see, by Gran Turismo 4's (and Prologue's) own rules where they differ from the shared engine's.

#include "test_framework.h"

#include "gt4r/race/display_mode_prologue.h"

using gt3r::race::HudMode;
using gt3r::race::HudPlay;
using gt3r::race::HudRun;
using gt4r::race::prologue_display_mode;

TEST(prologue_display_mode_by_its_race_classes) {
    CHECK(prologue_display_mode(HudRun::Race, HudPlay::Live) == HudMode::Race);
    CHECK(prologue_display_mode(HudRun::License, HudPlay::Live) == HudMode::License);
    CHECK(prologue_display_mode(HudRun::License, HudPlay::Replay) == HudMode::Replay);
    CHECK(prologue_display_mode(HudRun::Race, HudPlay::Demo) == HudMode::Demo);
    // No free run or time trial class: an arcade race's display.
    CHECK(prologue_display_mode(HudRun::TimeTrial, HudPlay::Live) == HudMode::Race);
    // The shared engine's rule makes a run of 10 laps the endurance display; Prologue keeps the test's (lesson 35, ten laps).
    gt3r::race::HudModeSelect gt3;
    gt3.run = HudRun::License, gt3.event_laps = 10;
    CHECK(gt3r::race::hud_mode(gt3) == HudMode::Endurance);
    CHECK(prologue_display_mode(HudRun::License, HudPlay::Live) == HudMode::License);
}
