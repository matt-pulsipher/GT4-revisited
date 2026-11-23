#include "gt4r/sim/ai/run.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gt4r::sim::ai {

FieldRun run_field(Disc& disc, const std::string& course, const std::vector<std::string>& cars, int laps, const Pace& pace, double limit,
                   const std::function<void(const Session&, const Field&)>& on_step) {
    std::unique_ptr<Session> session = Session::open(disc, course, cars);
    std::vector<size_t> computer(cars.size());
    for (size_t i = 0; i < computer.size(); ++i) computer[i] = i;
    std::unique_ptr<Field> field = Field::open(disc, course, *session, computer, pace);
    FieldRun out;
    out.course = course;
    out.lap_length = session->ground().road().length;
    out.notes = session->notes();
    out.cars.resize(cars.size());
    for (size_t i = 0; i < cars.size(); ++i) out.cars[i].car = cars[i];
    std::vector<race::CarInput> in(cars.size());
    for (int k = 0; k < 120; ++k) {
        field->hold(in);
        session->hold_once(in);
    }
    if (limit <= 0) limit = 400.0 * std::max(laps, 1) + 60.0;
    std::vector<int> last_lap(cars.size());
    std::vector<double> lap_start(cars.size(), 0.0);
    for (size_t i = 0; i < cars.size(); ++i) last_lap[i] = session->cars()[i].lap;
    const int steps = int(limit * 60.0);
    for (int step = 0; step < steps; ++step) {
        field->drive(in);
        session->step_once(in);
        if (on_step) on_step(*session, *field);
        const double t = session->time();
        bool all = true;
        for (size_t i = 0; i < cars.size(); ++i) {
            CarRun& r = out.cars[i];
            const race::CarState& st = session->cars()[i];
            r.top_speed = std::max(r.top_speed, st.speed);
            if (st.lap > last_lap[i] && !r.finished) {
                // A car that starts behind the line counts its laps from its first crossing; one ahead of it, from the start.
                if (last_lap[i] < 0) lap_start[i] = t;
                else {
                    r.laps.push_back(t - lap_start[i]);
                    lap_start[i] = t;
                    r.total += r.laps.back();
                }
                if (int(r.laps.size()) >= laps) r.finished = true;
            }
            last_lap[i] = st.lap;
            if (!r.finished) all = false;
        }
        out.seconds = t;
        if (all) break;
    }
    for (size_t i = 0; i < cars.size(); ++i) out.cars[i].incidents = field->incidents(i);
    return out;
}

} // namespace gt4r::sim::ai
