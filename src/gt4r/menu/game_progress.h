#pragma once

// A game played forward without its races: from power on through the game's own menus to a new
// game on the memory card, then cars bought, license tests passed and credits won the way the
// scripts and the program's result routines write them (each race's result made up), the
// presents given by Gran Turismo mode's present page, and the game saved by the card manager.
// For a saved game with progress in it: `gt4tool save <disc> progress`, and the tests.

#include "gt4r/menu/runtime.h"

#include <string>
#include <utility>
#include <vector>

namespace gt4r::menu {

struct ProgressPlan {
    std::string name = "GT4R"; // the player's name
    s64 credits = 5'000'000;   // the credits at the end
    // The cars bought new at the dealers (label, paint), in order; the last is driven.
    std::vector<std::pair<std::string, int>> cars;
    // The license categories passed ("l0b", "l0a", "lib", "lia", "l0s"), each test with this
    // result ("gold", "silver", "bronze") in the test's medal time.
    std::vector<std::string> licenses = {"l0b", "l0a", "lib", "lia", "l0s"};
    std::string license_result = "gold";
    // The events won, every race of them in first place (championships: every race and the
    // series): each event list of Gran Turismo mode's event data named here ("BeginnerHallEvents"),
    // all but the events in `events_not_won`.
    std::vector<std::string> event_lists = {"BeginnerHallEvents", "ProfessionalHallEvents"};
    std::vector<std::string> events_not_won = {"pr_gtworld_1000"};
    // Then events of the other lists won in turn (not those in `fill_lists_skipped`) until the
    // game's completion (the script's getRaceEventRecordStatistics, per mille) is this at least.
    int completion_permille = 0;
    std::vector<std::string> fill_lists_skipped;
    // The car driven at the end: the garage's car of this label when there is one (else the
    // last car bought).
    std::string driven;
};

struct ProgressReport {
    bool ok = false;
    std::string error;              // what stopped it
    std::vector<std::string> steps; // what was done, a line each
};

// The game's completion as Gran Turismo mode's script works it out (getRaceEventRecordStatistics,
// its cached figure dropped first): race wins per mille of all the events' races and the
// missions; -1 when Gran Turismo mode's project is not loaded.
int completion_permille(Runtime& rt);

// `rt` is made with a data folder whose memory card holds no game yet (`<data>/memorycard/`).
// On return the card holds the game, saved.
ProgressReport play_progress(Runtime& rt, const ProgressPlan& plan);

// A plan with a spread of cars (a small car, sports cars, a racing car; the US disc's labels: a
// car the disc does not have is not bought).
ProgressPlan default_progress_plan(Runtime& rt);

} // namespace gt4r::menu
