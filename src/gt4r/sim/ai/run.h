#pragma once

// A field of computer drivers run headless on a course, for checks: every car driven by the drivers from
// the course's grid, each car's lap times and incidents.

#include "gt4r/sim/ai/drivers.h"

#include <functional>
#include <string>
#include <vector>

namespace gt4r::sim::ai {

struct CarRun {
    std::string car;
    std::vector<double> laps;     // seconds, from the start to the first line crossing, then lap by lap
    double total = 0;             // to the last counted lap (0: not finished)
    bool finished = false;
    Incidents incidents;
    float top_speed = 0;          // the most it reached (m/s)
};

struct FieldRun {
    std::string course;
    float lap_length = 0;
    double seconds = 0;           // simulated
    std::vector<CarRun> cars;     // in grid order
    std::vector<std::string> notes;
};

// A car of a run: its name for the report and its entry (equipment, rolling start).
struct FieldCar {
    std::string name;
    Entry entry;
};

// Runs `cars` (labels, grid order, as sold) for `laps` laps on `course`, at most `limit` simulated seconds (0: 400 s a
// lap, more on a long course). From rest after a two-second hold (a rolling start when the entries' start_speed is
// set and the session places cars moving). Throws std::runtime_error when the course or a car cannot be set up.
FieldRun run_field(Disc& disc, const std::string& course, const std::vector<std::string>& cars, int laps, const Pace& pace = {},
                   double limit = 0, const std::function<void(const Session&, const Field&)>& on_step = {});
FieldRun run_field(Disc& disc, const std::string& course, const std::vector<FieldCar>& cars, int laps, const Pace& pace = {},
                   double limit = 0, const std::function<void(const Session&, const Field&)>& on_step = {});

// A disc's car database (the American names) and the program, for building entries.
db::SpecDb open_specdb(Disc& disc);

} // namespace gt4r::sim::ai
