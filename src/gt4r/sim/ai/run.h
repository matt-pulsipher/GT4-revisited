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

// Runs `cars` (labels, grid order) for `laps` laps on `course`, at most `limit` simulated seconds (0: 400 s a lap).
// A rolling start at `rolling_kmh` (0: from rest after a two-second hold). Throws std::runtime_error when the
// course or a car cannot be set up.
FieldRun run_field(Disc& disc, const std::string& course, const std::vector<std::string>& cars, int laps, const Pace& pace = {},
                   double limit = 0, const std::function<void(const Session&, const Field&)>& on_step = {});

} // namespace gt4r::sim::ai
