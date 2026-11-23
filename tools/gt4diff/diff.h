#pragma once

// A developer's check of our simulation against the user's own program: tools/gt4diff/diff.cpp.

#include "gt4r/disc/disc.h"

#include <map>
#include <string>
#include <vector>

namespace gt4r::diff {

// Every car's equipment as sold (the program's set-car) and spec (its spec builder, on a paved and
// on loose courses) against sim::stock_car and sim::build_spec, byte for byte.
struct SpecResult {
    bool ran = false; // the disc's program is a mapped build
    size_t cars = 0, equipment_equal = 0, spec_equal = 0;
};
SpecResult run_spec(Disc& disc, bool print);

// Every car's parameter block: the program's converter on its spec against the engine's converter on
// our translated spec with sim::gt4_params, field by field (equal: within a part in 100,000).
struct Field {
    std::string name;
    size_t values = 0, off = 0; // compared, and not equal
    double worst = 0;           // the largest relative difference
    std::string where;
};
struct ParamsResult {
    bool ran = false;
    size_t cars = 0, unfinished = 0;
    std::vector<Field> fields;
    const Field* field(const std::string& name) const {
        for (const Field& f : fields)
            if (f.name == name) return &f;
        return nullptr;
    }
};
ParamsResult run_params(Disc& disc, bool print);

// The race step: the program's own run in the interpreter with ours compared at every call (step.cpp):
// a standing start, the gears, a lift, turns each way and the brakes, on a course ("tsukuba") with a car
// ("nsx_r_02"; the environment's GT4R_STEP_COURSE / GT4R_STEP_CAR when empty).
struct StepTap {
    std::string name;
    size_t calls = 0, equal = 0; // calls, and those with every compared field and result equal
};
struct StepResult {
    bool ran = false;
    std::string error;
    std::vector<StepTap> taps;
};
StepResult run_step(Disc& disc, bool print, const std::string& course = "", const std::string& car = "", int steps = 0);

// The computer drivers: a field of the program's own drivers run in the program's own race (every car a computer's, from
// a standing start), each car's lap times (ai.cpp).
struct AiCarRun {
    std::string car;
    std::vector<double> laps; // from the first line crossing, lap by lap
    double total = 0;
    bool finished = false;
    float top_speed = 0;
};
struct AiRunOptions {
    bool ours = false;   // our drivers in the program's race (the decision 0x465078 replaced)
    bool checks = false; // the lines, parameters, arcs' speeds and braking compared with ours, bit for bit
    bool band = false;   // the conductor's own rubber band kept (else off)
};
struct AiCheck {
    u64 compared = 0, differ = 0;
};
struct AiRunResult {
    bool ran = false;
    std::string error;
    float lap_length = 0;
    double seconds = 0;
    std::vector<AiCarRun> cars;
    std::map<std::string, AiCheck> checks;
};
// (GT4R_AI_OURS, GT4R_AI_CHECK, GT4R_AI_BAND set the options too; GT4R_AI_TRACE=<car> prints a car's plan.)
AiRunResult run_ai(Disc& disc, bool print, const std::string& course, const std::vector<std::string>& cars, int laps, AiRunOptions options = {});

// The C library's float routines the step calls, against the shared engine's.
struct LeavesResult {
    bool ran = false;
    size_t samples = 0, sin_off = 0, cos_off = 0, tan_off = 0;
    size_t atan2_off[3] = {}; // against Gran Turismo 3's, the later builds' and 2002 Tokyo-Geneva's
};
LeavesResult run_leaves(Disc& disc, bool print);

} // namespace gt4r::diff
