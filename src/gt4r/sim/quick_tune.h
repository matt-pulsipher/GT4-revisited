#pragma once

// Arcade's Quick Tune (the race menus' Quick Tune page, `QuickTunePopup` of the projects quick-arcade and quick-tt): what the
// page offers, which the sequence fills into the menus' work before them (the single race's 0x111B50, the time trial's
// 0x112FE8), and what the program does with the player's choice when a run starts (0x111DC0, 0x113248). Only the player's car
// changes; the change stays on it for the rest of the sequence. docs/formats/race_flow.md "Quick Tune".

#include "gt4r/sim/equipment.h"
#include "gt4r/sim/pit_tires.h"
#include "gt4r/sim/program.h"

namespace gt4r::sim {

// The page's choice as the menus' work holds it.
struct QuickTune {
    int power_level = 0;  // QuickTunePowerLevel (work +0x3B0): the output in percent more (or less) than the car's
    int weight_level = 0; // QuickTuneWeightLevel (+0x3AC): the weight likewise
    int front_tire = -1, rear_tire = -1; // QuickTuneFrontTireType / RearTireType (+0x3B8 / +0x3BC): tire kinds 0 .. 12
    int gear_top_speed = 0; // QuickTuneGearMaxSpeed (+0x3B4): the gearbox slider (x 10 km/h); 0 leaves the gears
};

// The gearbox's slider as the page offers it (QuickTuneGearMaxSpeed, its Min and Max, work +0x3B4 .. +0x3B6): the equipment's
// slider (+0x12A) and the GEAR row's least and greatest (+0x26, +0x27); all 0 for a gearbox whose ratios are not made from the
// slider (GEAR +0x25 0), which the page shows insensitive.
struct GearSliderRange {
    int value = 0, min = 0, max = 0;
};
GearSliderRange quick_tune_gears(db::SpecDb& db, const Equipment& eq);

// The choice applied to the player's equipment (0x111DC0): the power percentage (+0x163) 100 + the power level and the weight
// percentage (+0x162) 100 + the weight level, each a byte; the tire kinds (+0x160 front, +0x161 rear, flag +0x165), fitted as
// the race fits them (RaceArcade::vf92: dirt's and snow's kinds on those courses, `course_kind` COURSE +0x10); a slider other
// than 0 into +0x12A and the gears made from it (0x443240).
void apply_quick_tune(db::SpecDb& db, const Program& program, const FileReader& read, Equipment& eq, const QuickTune& q, int course_kind);

} // namespace gt4r::sim
