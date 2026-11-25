#pragma once

// A demonstration run (a license test's or a mission's "View a demo run") played as the program plays it: not driven,
// replayed. The disc keeps the run as the drivers' recorded pad records (replay/license/<event label>,
// formats/replay/demonstration.h); the race is set up with the file's entries (each car's equipment, its place, its
// gearbox and aids) and car 0 is given the recorded record at every race step, from the first step of the race (the
// start's hold and count included), until the recording runs out; then the empty record (no throttle, no steering).
// docs/specs/ai.md "The demonstration runs".
//
// For the race screen:
//   auto demo = sim::ai::DemoRun::load(disc, program, "usl0b0001");
//   Session session(db, program, road, demo->entries());        // the file's cars, not the event's
//   for (size_t step = 0;; ++step) {
//       inputs[0] = demo->input(step);                          // car 0: the recording
//       (other cars: the computer drivers as in any race: the pace car of a "training" test)
//       session.step_once(inputs);
//       if (demo->over(step)) break;                            // the recording ran out: the demonstration ends
//   }

#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/race/simulation.h"
#include "gt4r/sim/session.h"

#include <optional>
#include <string>
#include <vector>

namespace gt4r {
class Disc;
}

namespace gt4r::sim::ai {

// A recorded pad record as the seam's input: the steering (the record's + left, the seam's + right), throttle and
// brake over 4096, the shift's request held for the step it is in the record. `raw` (the record) rides along for an
// input phase that reads the record itself.
race::CarInput pad_input(const replay::PadRecord& r);

struct DemoRun {
    std::string label;          // the event: a license test's label ("usl0b0001")
    replay::Demonstration demo; // the file

    // The race's entries as the file has them: each car's equipment, its grid place, its gearbox (pedal style 0: AT)
    // and its aids (+0x3450 ASM, +0x3454 TCS).
    std::vector<Entry> entries() const;
    // Car 0's input at race step `step` (0: the race's first step): the recorded record, then the empty one.
    race::CarInput input(size_t step) const;
    const replay::PadRecord& record(size_t step) const;
    // The recording's steps (its stream's T - 1 frames).
    size_t steps() const { return demo.streams.empty() ? 0 : demo.streams[0].frames.size(); }
    // The recording has run out at this step: the program's playback is done (0x345D70 sets its flag) and the
    // demonstration's race ends.
    bool over(size_t step) const { return step >= steps(); }
    // The time the recorded run took (ms; -1 for a run the file gives none).
    int recorded_ms() const { return demo.time_ms; }

    // nullopt (with *error) without the file, or when it does not read (a program whose cipher tables are not mapped).
    static std::optional<DemoRun> load(Disc& disc, const Program& program, const std::string& label, std::string* error = nullptr);
};

} // namespace gt4r::sim::ai
