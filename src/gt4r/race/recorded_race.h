#pragma once

// A recorded race played without a picture (docs/formats/replay.md "Playing one"): an arcade single race's or a free run's
// recording (Serialize::ReplayData: the Replay Theater's demo replays, a saved replay), the race set up as the race screen
// sets it up (the start, the session, the drivers, the pit stops, the flow), the player's car by its records (or, a B-spec
// run's, by its driver and the recorded commands). For `gt4tool race replays`, gt4r_diff and the tests; the race screen plays
// the same pieces.

#include "gt4r/race/race_flow.h"
#include "gt4r/sim/ai/demo_driver.h"
#include "gt4r/sim/session.h"

#include <memory>
#include <string>
#include <vector>

namespace gt4r {
class Disc;
namespace db {
class SpecDb;
}
namespace sim::ai {
class Field;
}
} // namespace gt4r

namespace gt4r::race {

class RecordedRace {
public:
    // Throws std::runtime_error for a recording that cannot be set up (its course or a car not on the disc).
    RecordedRace(Disc& disc, db::SpecDb& db, sim::ai::DemoRun run);
    ~RecordedRace();

    void step(); // the next record's step
    bool over() const { return run_.over(at_); }
    size_t steps() const { return at_; } // the steps run
    const sim::ai::DemoRun& run() const { return run_; }
    const std::string& course() const { return course_; }
    sim::Session& session();
    sim::ai::Field* field();
    const RaceFlow& flow() const { return flow_; }
    const std::vector<s64>& laps() const { return laps_; } // the player's lap times, ms
    s64 total_ms() const; // the player's race time, -1 before his finish
    // The run came out as recorded: the file's time is the race's when it was run to its end, else the last lap's; and (+0xC4)
    // the best lap's.
    bool exact() const;

private:
    struct Parts;
    sim::ai::DemoRun run_;
    std::string course_;
    std::unique_ptr<Parts> p_;
    RaceFlow flow_;
    size_t at_ = 0;
    std::vector<s64> laps_;
};

} // namespace gt4r::race
