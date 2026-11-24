// gt4tool sweep: every course and every car run headless, for the playtest's sweeps. One line a course or a car,
// tab separated, flushed as it is done, so a run that dies leaves what it finished (and `from=` resumes it).
//
//   gt4tool sweep <disc> courses [filter] [cars=4] [laps=1] [from=N] [count=N]
//       every course file of crs/ (whose name contains [filter]): read, its road and driver file set up, a field of
//       computer drivers run [laps] laps from the grid. Columns: course, the course's name in the database (or -),
//       load ms, run ms, lap length m, simulated s, finished cars, best lap s, worst lap s, top km/h, stuck s (most of a
//       car), wall hits, car hits, spins, off-road, flags, where the spins began (m along the course), error.
//   gt4tool sweep <disc> cars [filter] [course=tsukuba] [seconds=40] [from=N] [count=N]
//       every car of the database (whose label contains [filter]), as sold, alone on the course's grid, driven by the
//       course's computer driver for [seconds]. Columns: index, label, name, build ms, run ms, metres covered, top km/h,
//       0-100 km/h s, stuck s, wall hits, spins, off-road, laps, flags, where the spins began, error.
//
// Flags: ERROR (could not be set up), NAN (a car's state not a number), STILL (under 50 m covered), SLOW (top under
// 60 km/h), STUCK (over 5 s under 5 km/h after the start), SPINS (more than 2), WALLS (more than 10 wall hits),
// NOLAP (a course run where no car finished), NODRIVER (a course without a driver file: only loaded).

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

using Args = std::vector<std::string>;
using Clock = std::chrono::steady_clock;

double ms_since(Clock::time_point t) { return std::chrono::duration<double, std::milli>(Clock::now() - t).count(); }

double option(const Args& a, const char* name, double def) {
    const std::string key = std::string(name) + "=";
    for (const std::string& x : a)
        if (x.rfind(key, 0) == 0) return std::atof(x.c_str() + key.size());
    return def;
}

std::string option_str(const Args& a, const char* name, const std::string& def) {
    const std::string key = std::string(name) + "=";
    for (const std::string& x : a)
        if (x.rfind(key, 0) == 0) return x.substr(key.size());
    return def;
}

// The first argument after the mode that is not an option: the filter.
std::string filter_of(const Args& a) {
    for (size_t i = 1; i < a.size(); ++i)
        if (a[i].find('=') == std::string::npos) return a[i];
    return "";
}

// A course's road (RNW4, or Prologue's GTRW), shared by its sessions.
std::shared_ptr<const course::Runway> load_road(Disc& disc, const std::string& course) {
    const gt3r::Volume::Entry* file = disc.volume().find("crs/" + course);
    if (!file) throw std::runtime_error("no file crs/" + course);
    const Bytes data = disc.volume().read(*file);
    if (!course::CourseFile::looks_like_course(data)) throw std::runtime_error("not a course");
    const course::CourseFile parsed = course::CourseFile::parse(data);
    std::optional<course::Runway> road = course::course_road(parsed, data);
    if (!road) throw std::runtime_error("no road");
    return std::make_shared<const course::Runway>(std::move(*road));
}

bool finite_state(const race::CarState& s) {
    return std::isfinite(s.position.x) && std::isfinite(s.position.y) && std::isfinite(s.position.z) && std::isfinite(s.speed);
}

// What one driven run measured, per car.
struct Measure {
    double metres = 0, top = 0, to100 = -1, stuck = 0;
    int laps = 0;
    std::vector<double> lap_times;
    bool nan = false;
    std::vector<int> spin_at; // metres along the course where its spins began (the first few)
};

// The places of a run's spins, every car's together ("1210,1215,2890"), at most 8.
std::string spin_places(const std::vector<Measure>& m) {
    std::vector<int> all;
    for (const Measure& x : m) all.insert(all.end(), x.spin_at.begin(), x.spin_at.end());
    std::sort(all.begin(), all.end());
    std::string out;
    for (size_t i = 0; i < all.size() && i < 8; ++i) out += (out.empty() ? "" : ",") + std::to_string(all[i]);
    return out.empty() ? "-" : out;
}

// A session of `entries` on `road` with the course's drivers on every car, run `seconds` (or until every car has done
// `laps` laps when laps > 0). Throws as Session and Field do.
std::vector<Measure> drive(Disc& disc, db::SpecDb& db, const sim::Program& program, const std::string& course,
                           const std::shared_ptr<const course::Runway>& road, const std::vector<sim::Entry>& entries, double seconds,
                           int laps, std::vector<sim::ai::Incidents>& incidents) {
    sim::Session session(db, program, road, entries);
    std::vector<size_t> computer(entries.size());
    for (size_t i = 0; i < computer.size(); ++i) computer[i] = i;
    std::unique_ptr<sim::ai::Field> field = sim::ai::Field::open(disc, course, session, computer, {});
    for (size_t car : computer) session.set_driver_aids(car, field->params(car).aids, field->params(car).grip);
    std::vector<race::CarInput> in(entries.size());
    for (int k = 0; k < sim::Session::kHoldSteps; ++k) {
        field->hold(in);
        session.hold_once(in);
    }
    std::vector<Measure> m(entries.size());
    std::vector<int> last_lap(entries.size());
    std::vector<double> lap_start(entries.size(), 0.0);
    std::vector<int> spins(entries.size(), 0);
    for (size_t i = 0; i < entries.size(); ++i) last_lap[i] = session.cars()[i].lap;
    const int steps = int(seconds * 60.0);
    for (int step = 0; step < steps; ++step) {
        field->drive(in);
        field->apply_time_scales(session);
        session.step_once(in);
        const double t = session.time();
        bool all = laps > 0;
        for (size_t i = 0; i < entries.size(); ++i) {
            const race::CarState& s = session.cars()[i];
            Measure& r = m[i];
            if (!finite_state(s)) {
                r.nan = true;
                continue;
            }
            const double v = std::abs(double(s.speed));
            r.metres += v / 60.0;
            r.top = std::max(r.top, v * 3.6);
            if (r.to100 < 0 && v * 3.6 >= 100.0) r.to100 = t;
            if (t > 5.0 && v * 3.6 < 5.0) r.stuck += 1.0 / 60.0;
            if (s.lap > last_lap[i]) {
                if (last_lap[i] >= 0) r.lap_times.push_back(t - lap_start[i]);
                lap_start[i] = t;
            }
            last_lap[i] = s.lap;
            r.laps = int(r.lap_times.size());
            if (const int n = field->incidents(i).spins; n > spins[i]) {
                spins[i] = n;
                if (r.spin_at.size() < 8) r.spin_at.push_back(int(s.course_v));
            }
            if (laps > 0 && r.laps < laps) all = false;
        }
        if (all) break;
    }
    incidents.clear();
    for (size_t i = 0; i < entries.size(); ++i) incidents.push_back(field->incidents(i));
    return m;
}

int sweep_courses(Disc& disc, const Args& a) {
    const std::string filter = filter_of(a);
    const int ncars = std::clamp(int(option(a, "cars", 4)), 1, 16);
    const int laps = std::max(1, int(option(a, "laps", 1)));
    const int from = int(option(a, "from", 0)), count = int(option(a, "count", 1 << 30));
    const std::vector<std::string> field_labels = {"lancer_evo8_gsr_03", "nsx_r_02", "civic_r_98", "integra_r_98", "impreza_wrx_sti_04",
                                                   "s2000_99", "rx7_r_93", "supra_rz_97"};
    db::SpecDb db = sim::ai::open_specdb(disc);
    const sim::Program program = sim::Program::load(disc);
    std::map<std::string, std::string> names;
    for (const db::CourseEntry& c : db.courses()) names[c.label] = c.name;
    std::vector<sim::Entry> entries;
    for (int i = 0; i < ncars; ++i) {
        sim::Entry e;
        const std::string& label = field_labels[size_t(i) % field_labels.size()];
        if (!sim::stock_car(db, program, label, e.equipment)) {
            std::fprintf(stderr, "no car %s\n", label.c_str());
            return 1;
        }
        entries.push_back(e);
    }
    std::vector<std::string> courses;
    for (const std::string& name : disc.volume().list_dir("crs/")) {
        if (name.find('.') != std::string::npos || name.find('/') != std::string::npos) continue;
        if (!filter.empty() && name.find(filter) == std::string::npos) continue;
        courses.push_back(name);
    }
    std::sort(courses.begin(), courses.end());
    std::printf("#course\tname\tload_ms\trun_ms\tlap_m\tsim_s\tfinished\tbest_s\tworst_s\ttop_kmh\tstuck_s\twalls\tcar_hits\tspins\toff_road\tflags\tspins_at_m\terror\n");
    std::fflush(stdout);
    int index = -1, done = 0;
    for (const std::string& course : courses) {
        if (++index < from || done >= count) continue;
        ++done;
        std::fprintf(stderr, "start %d %s\n", index, course.c_str());
        const auto t0 = Clock::now();
        std::string error, flags;
        double load_ms = 0, run_ms = 0, lap_m = 0, sim_s = 0, best = 0, worst = 0, top = 0, stuck = 0;
        int finished = 0, walls = 0, car_hits = 0, spins = 0, off = 0;
        std::string where = "-";
        try {
            const std::shared_ptr<const course::Runway> road = load_road(disc, course);
            lap_m = road->length;
            const bool has_driver = sim::ai::Field::read(disc, course).has_value();
            load_ms = ms_since(t0);
            if (!has_driver) {
                // Loaded only: the session set up and held, no drivers.
                sim::Session session(db, program, road, entries);
                flags += "NODRIVER ";
            } else {
                const auto t1 = Clock::now();
                std::vector<sim::ai::Incidents> inc;
                const double limit = std::max(400.0, double(lap_m) / 5.0) * laps + 60.0;
                const std::vector<Measure> m = drive(disc, db, program, course, road, entries, limit, laps, inc);
                run_ms = ms_since(t1);
                best = 1e9;
                for (size_t i = 0; i < m.size(); ++i) {
                    if (m[i].nan) flags += "NAN ";
                    if (m[i].laps >= laps) ++finished;
                    for (double l : m[i].lap_times) best = std::min(best, l), worst = std::max(worst, l), sim_s = std::max(sim_s, l);
                    top = std::max(top, m[i].top);
                    stuck = std::max(stuck, m[i].stuck);
                    walls += inc[i].wall_hits, car_hits += inc[i].car_hits, spins += inc[i].spins, off += inc[i].off_road;
                }
                if (best > 1e8) best = 0;
                where = spin_places(m);
                if (finished == 0) flags += "NOLAP ";
                if (stuck > 5) flags += "STUCK ";
                if (spins > 2 * int(m.size())) flags += "SPINS ";
            }
        } catch (const std::exception& e) {
            error = e.what();
            flags += "ERROR ";
        }
        if (!flags.empty()) flags.pop_back();
        const auto n = names.find(course);
        std::printf("%s\t%s\t%.0f\t%.0f\t%.0f\t%.1f\t%d/%d\t%.3f\t%.3f\t%.0f\t%.1f\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", course.c_str(),
                    n == names.end() || n->second.empty() ? "-" : n->second.c_str(), load_ms, run_ms, lap_m, sim_s, finished, ncars, best, worst, top, stuck, walls,
                    car_hits, spins, off, flags.empty() ? "-" : flags.c_str(), where.c_str(), error.empty() ? "-" : error.c_str());
        std::fflush(stdout);
    }
    return 0;
}

int sweep_cars(Disc& disc, const Args& a) {
    const std::string filter = filter_of(a);
    const std::string course = option_str(a, "course", "tsukuba");
    const double seconds = option(a, "seconds", 40);
    const int from = int(option(a, "from", 0)), count = int(option(a, "count", 1 << 30));
    db::SpecDb db = sim::ai::open_specdb(disc);
    const sim::Program program = sim::Program::load(disc);
    const std::shared_ptr<const course::Runway> road = load_road(disc, course);
    std::printf("#index\tlabel\tname\tbuild_ms\trun_ms\tmetres\ttop_kmh\tto100_s\tstuck_s\twalls\tspins\toff_road\tlaps\tflags\tspins_at_m\terror\n");
    std::fflush(stdout);
    int index = -1, done = 0;
    for (const db::CarEntry& c : db.cars()) {
        ++index;
        if (!filter.empty() && c.label.find(filter) == std::string::npos) continue;
        if (index < from || done >= count) continue;
        ++done;
        std::fprintf(stderr, "start %d %s\n", index, c.label.c_str());
        std::string error, flags;
        double build_ms = 0, run_ms = 0;
        Measure m;
        sim::ai::Incidents inc;
        try {
            const auto t0 = Clock::now();
            sim::Entry e;
            if (!sim::stock_car(db, program, c.label, e.equipment)) throw std::runtime_error("the car cannot be built");
            build_ms = ms_since(t0);
            const auto t1 = Clock::now();
            std::vector<sim::ai::Incidents> incs;
            m = drive(disc, db, program, course, road, {e}, seconds, 0, incs).at(0);
            inc = incs.at(0);
            run_ms = ms_since(t1);
            if (m.nan) flags += "NAN ";
            if (m.metres < 50) flags += "STILL ";
            else if (m.top < 60) flags += "SLOW ";
            if (m.stuck > 5) flags += "STUCK ";
            if (inc.spins > 2) flags += "SPINS ";
            if (inc.wall_hits > 10) flags += "WALLS ";
        } catch (const std::exception& e) {
            error = e.what();
            flags += "ERROR ";
        }
        if (!flags.empty()) flags.pop_back();
        std::printf("%d\t%s\t%s\t%.0f\t%.0f\t%.0f\t%.0f\t%.2f\t%.1f\t%d\t%d\t%d\t%d\t%s\t%s\t%s\n", index, c.label.c_str(), c.name.c_str(), build_ms, run_ms,
                    m.metres, m.top, m.to100, m.stuck, inc.wall_hits, inc.spins, inc.off_road, m.laps, flags.empty() ? "-" : flags.c_str(),
                    spin_places({m}).c_str(), error.empty() ? "-" : error.c_str());
        std::fflush(stdout);
    }
    return 0;
}

} // namespace

int cmd_sweep(Disc& disc, const std::vector<std::string>& a) {
    if (a.empty()) return 2;
    if (a[0] == "courses") return sweep_courses(disc, a);
    if (a[0] == "cars") return sweep_cars(disc, a);
    std::fprintf(stderr, "sweep: courses | cars\n");
    return 2;
}

} // namespace gt4r::tool
