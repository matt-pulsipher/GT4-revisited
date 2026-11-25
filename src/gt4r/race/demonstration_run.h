#pragma once

// A license test's demonstration played without a picture: the race set up as the race screen sets it up (the starts, the
// session, the drivers, the flow, the judge, the cones), the player's car given the recording, until the run ends or the
// recording does. For `gt4tool race demos` and the tests; the race screen plays the same pieces.

#include "gt4r/race/simulation.h"
#include "gt4r/sim/session.h"

#include <functional>
#include <string>

namespace gt4r {
class Disc;
namespace db {
class SpecDb;
}
namespace sim::ai {
struct DemoRun;
}
} // namespace gt4r

namespace gt4r::race {

struct DemonstrationRun {
    bool ran = false;
    std::string error, course, car, equipment; // (equipment: Prologue's, where the file's parts or settings differ from the event's pool)
    int kind = 0;           // the test's kind (LicenseRules::kind)
    int recorded_ms = -1;   // the file's time
    int ended_ms = -1;      // the run's time (-1: it did not end timed)
    int fail = 0;           // the judge's failure (LicenseFail; 0 none)
    size_t steps = 0;       // the steps run
    bool exact() const { return ended_ms >= 0 && ended_ms == recorded_ms; }
};

struct DemonstrationOptions {
    int extra = 60; // steps past the recording's end
    bool cones = true;
    // Each step after it ran (0: the race's first), for traces.
    std::function<void(size_t step, const sim::Session&)> on_step;
    const sim::ai::DemoRun* run = nullptr; // another recording of the test (a saved replay's), in place of the disc's
};

// Plays replay/license/<label> ("usl0b0001").
DemonstrationRun run_demonstration(Disc& disc, db::SpecDb& db, const std::string& label, const DemonstrationOptions& options = {});

} // namespace gt4r::race
