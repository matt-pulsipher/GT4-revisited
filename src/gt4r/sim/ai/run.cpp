#include "gt4r/sim/ai/run.h"

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/rules_start.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace gt4r::sim::ai {

db::SpecDb open_specdb(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

FieldRun run_field(Disc& disc, const std::string& course, const std::vector<std::string>& labels, int laps, const Pace& pace, double limit,
                   const std::function<void(const Session&, const Field&)>& on_step) {
    db::SpecDb db = open_specdb(disc);
    const Program program = Program::load(disc);
    std::vector<FieldCar> cars(labels.size());
    for (size_t i = 0; i < labels.size(); ++i) {
        cars[i].name = labels[i];
        if (!stock_car(db, program, labels[i], cars[i].entry.equipment)) throw std::runtime_error("run: no car " + labels[i]);
    }
    return run_field(disc, course, cars, laps, pace, limit, on_step);
}

FieldRun run_field(Disc& disc, const std::string& course, const std::vector<FieldCar>& field_cars, int laps, const Pace& pace, double limit,
                   const std::function<void(const Session&, const Field&)>& on_step, const std::function<void(Session&, Field&)>& control) {
    db::SpecDb db = open_specdb(disc);
    const Program program = Program::load(disc);
    gt3r::Volume* vol = &disc.volume();
    const gt3r::Volume::Entry* file = vol->find("crs/" + course);
    if (!file) throw std::runtime_error("run: no course " + course);
    const Bytes data = vol->read(*file);
    if (!course::CourseFile::looks_like_course(data)) throw std::runtime_error("run: " + course + " is not a course");
    const course::CourseFile parsed = course::CourseFile::parse(data);
    std::optional<course::Runway> road = course::course_road(parsed, data); // (RNW4, or Prologue's GTRW)
    if (!road) throw std::runtime_error("run: the course " + course + " has no road");
    std::vector<Entry> entries;
    std::vector<std::string> cars;
    for (const FieldCar& c : field_cars) entries.push_back(c.entry), cars.push_back(c.name);
    // A rolling start without places of its own: the race's rule (the last place 400 m before the line, 20 m apart).
    for (size_t i = 0; i < entries.size(); ++i)
        if (entries[i].start_speed > 0 && std::isnan(entries[i].start_distance))
            entries[i].start_distance = float(rules::rolling_start_distance(-400, 20, int(entries.size()), entries[i].grid_place >= 0 ? entries[i].grid_place : int(i)));
    auto session = std::make_unique<Session>(db, program, std::make_shared<const course::Runway>(std::move(*road)), entries, course_row(db, course));
    std::vector<size_t> computer(cars.size());
    for (size_t i = 0; i < computer.size(); ++i) computer[i] = i;
    bool rolling = false;
    for (const Entry& e : entries) rolling = rolling || e.start_speed > 0;
    Pace p = pace;
    if (rolling) p.standing_start = false;
    std::unique_ptr<Field> field = Field::open(disc, course, *session, computer, p);
    field->apply_start_delays(*session);
    for (size_t car : computer) session->set_driver_aids(car, field->params(car).aids, field->params(car).grip); // (their aids, 0x3F7828)
    FieldRun out;
    out.course = course;
    out.lap_length = session->ground().road().length;
    out.notes = session->notes();
    out.cars.resize(cars.size());
    for (size_t i = 0; i < cars.size(); ++i) out.cars[i].car = cars[i];
    std::vector<race::CarInput> in(cars.size());
    // A standing start's two seconds held; a rolling start goes at once.
    for (int k = 0; k < (rolling ? 0 : 120); ++k) {
        field->hold(in);
        session->hold_once(in);
    }
    // At least 400 s a lap, more on a long course (5 m/s on average).
    if (limit <= 0) limit = std::max(400.0, double(out.lap_length) / 5.0) * std::max(laps, 1) + 60.0;
    std::vector<int> last_lap(cars.size());
    std::vector<double> lap_start(cars.size(), 0.0);
    for (size_t i = 0; i < cars.size(); ++i) last_lap[i] = session->cars()[i].lap;
    const int steps = int(limit * 60.0);
    for (int step = 0; step < steps; ++step) {
        if (control) control(*session, *field);
        field->drive(in);
        field->apply_time_scales(*session);
        session->step_once(in);
        if (on_step) on_step(*session, *field);
        const double t = session->time();
        bool all = true;
        for (size_t i = 0; i < cars.size(); ++i) {
            CarRun& r = out.cars[i];
            const race::CarState& st = session->cars()[i];
            r.top_speed = std::max(r.top_speed, st.speed);
            if (st.lap > last_lap[i] && rolling) session->release_start(int(i)); // (let go at its crossing: the conductor's virtual 20)
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
