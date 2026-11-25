#pragma once

// The console's floats against recordings made on the console (tools/gt4diff/floats*.cpp; docs/specs/physics.md
// "Floats"): the disc's license demonstrations played through the program's own race step under the license
// conductor in the shared engine's interpreter (its EE float model), each run judged by the program's own judge and
// scored against the time the file records; a census of the float operations the race step executes (the FPU's, the
// VU0 macro instructions', the soft doubles' calls).

#include "gt4r/disc/disc.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace gt3r::diff {
class Ee;
}

namespace gt4r::diff {

// One demonstration played through the program (run_float_demo).
struct DemoScore {
    bool ran = false;
    std::string error;
    std::string label, race_class, course, car;
    int kind = 0;           // the test's kind (CFG +0x02: 1 a stop in the box, 2 a goal, 3 laps, 6 behind the pace car)
    int frames = 0;         // the recording's records
    int recorded_ms = -1;   // the file's time (+0xC0; -1 none)
    int ended_ms = -1;      // the program's judged time (0x3F8AB0's), -1 when the run did not end timed
    int ended_step = -1;    // the step the run ended at (timed or failed)
    int fail_kind = -1;     // the judge's failure (0x3F8980's kind), -1 none
    int hold = 0;           // the start's hold the run was given (steps)
    bool exact() const { return ended_ms >= 0 && ended_ms == recorded_ms; }
};

struct FloatDemoOptions {
    int hold = -1;          // the start's hold in steps (race information +0xBC); -1: the license tests' (480)
    int extra = 600;        // steps played past the recording's end
    bool verbose = false;   // the run's steps every second
    // Called once the race is set up, before its first step (to install probes; FloatCensus::attach).
    std::function<void(gt3r::diff::Ee&)> on_setup;
};

// Plays replay/license/<label> (a license test's demonstration) through the program's race step: the event's row
// copied into the conductor's configuration (0x34D418), the file's entries (equipment, pedal style, driver kind, the
// aids' switches, the condition block), the license conductor (a training test's for the tests behind the pace car),
// the start's hold counted down before each step as the race's frame does, entry 0's controller fed the recorded
// records. Ends when the program's judge ends the run (0x3F8AB0 timed, 0x3F8980 failed) or the records and `extra`
// steps are played.
DemoScore run_float_demo(Disc& disc, const std::string& label, const FloatDemoOptions& options = {}, bool print = false);

// The demonstrations the program can ask for: replay/license/<label> named as an event of the database.
std::vector<std::string> demonstration_labels(Disc& disc);

// A census of the float operations the interpreter runs (FloatCensus::attach on a race): every FPU and VU0 float
// instruction of the program's text, counted per address and per kind (lanes for VU0), and, with a list of functions
// (GT4R_FLOATS_FUNCTIONS=<functions.tsv of the analysis>), the calls of each function (the soft doubles among them).
struct FloatCensus {
    // kinds: "fpu.add" (add/sub/adda/suba), "fpu.mul" (mul/mula), "fpu.madd" (madd/msub/madda/msuba), "fpu.div",
    // "fpu.sqrt", "fpu.rsqrt", "fpu.cvt" (cvt.s.w / cvt.w.s), "fpu.cmp" (c.*, max, min, abs, neg, mov), "vu.add",
    // "vu.mul", "vu.madd", "vu.opm" (opmula/opmsub), "vu.div", "vu.sqrt", "vu.rsqrt", "vu.cvt" (ftoi/itof), "vu.other"
    std::map<u32, u64> at;                 // executions per instruction address
    std::map<u32, std::string> kind_at;    // the kind of each counted address
    std::map<std::string, u64> by_kind;    // executions (VU0: lanes) per kind
    std::map<u32, u64> calls;              // function entries (with a function list)
    u64 steps = 0;
    // Installs the probes on `ee` (the program's text scanned once).
    void attach(gt3r::diff::Ee& ee);
    // The float instructions of the text (address -> kind), scanned from the loaded program.
    static std::map<u32, std::string> scan(gt3r::diff::Ee& ee, u32 text_begin, u32 text_end);
};

// gt4r_diff <disc> floats [demos] [label,label,... | all]: every demonstration scored (the default: all);
// gt4r_diff <disc> floats census <label>: the float operations of that demonstration's run, per kind and per address
// (GT4R_FLOATS_OUT=<file>: the addresses' counts as "address kind count" lines, and the functions' calls).
int run_floats(Disc& disc, const std::vector<std::string>& args);

} // namespace gt4r::diff
