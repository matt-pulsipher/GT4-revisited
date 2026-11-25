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
//   gt4tool sweep <disc> pad [course,course..] [car=nsx_r_02] [cars=4] [laps=2] [line=1]
//       the player's car driven through the pad's path (an analog stick, throttle and brake: steering to the road's
//       middle, easing and braking for bends; with the manual gearbox, shifting up 300 rpm under the rev limit and
//       down under 55 % of the power's speed) against computer drivers, in each gearbox (AT, MT) with the aids on
//       and off. Columns: course, gearbox, aids, laps done, lap times s, top km/h, stuck s, wall contacts, car
//       contacts, gear changes, place at the end (of `cars`), spins (the slip angle past 80 degrees), seconds sliding
//       (past 15 degrees), flags.
//   gt4tool sweep <disc> stop [course=tsukuba] [car=ka_01] [run=8] [idle=4] [hold=1] [launch=8] [manual=0] [aids=1]
//       the player's car alone through the pad's path: driven, braked to a full stop, stood (the brake held or not),
//       then a full-throttle launch; a line every quarter second (gear, speed, rpm, the reverse request).
//   gt4tool sweep <disc> wall [course=tsukuba] [car=integra_r_98] [event=<label>] [run=6] [after=10] [steer=1]
//       a wall hit under an event's rules (the penalty and its speed cap) and the speed after it.
//   gt4tool sweep <disc> grid [filter] [cars=6]
//       where every course's grid places stand along the course (m from the line, negative behind it / the session's
//       lap there) and how many are past the line.
//
// Flags: ERROR (could not be set up), NAN (a car's state not a number), STILL (under 50 m covered), SLOW (top under
// 60 km/h), STUCK (over 5 s under 5 km/h after the start), SPINS (more than 2), WALLS (more than 10 wall hits),
// NOLAP (a course run where no car finished), NODRIVER (a course without a driver file: only loaded).

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/line_driver.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/race_rules.h"
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

std::vector<std::string_view> gt3r_split(std::string_view s) {
    std::vector<std::string_view> out;
    while (!s.empty()) {
        const size_t c = s.find(',');
        if (c) out.push_back(s.substr(0, c));
        if (c == std::string_view::npos) break;
        s = s.substr(c + 1);
    }
    return out;
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
    sim::Session session(db, program, road, entries, sim::course_row(db, course));
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
                                                   "s2000_a_99", "rx7_r_eu_93", "supra_rz_97"};
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
                sim::Session session(db, program, road, entries, sim::course_row(db, course));
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

int sweep_pad(Disc& disc, const Args& a) {
    std::vector<std::string> courses;
    const std::string wanted = filter_of(a);
    for (std::string_view c : gt3r_split(wanted)) courses.emplace_back(c);
    if (courses.empty()) courses = {"tsukuba"};
    const std::string car = option_str(a, "car", "nsx_r_02");
    const int ncars = std::clamp(int(option(a, "cars", 4)), 1, 16);
    const int laps = std::max(1, int(option(a, "laps", 2)));
    const bool line = option(a, "line", 1) != 0; // 0: no line heading, the active steering off
    const std::vector<std::string> field_labels = {"integra_r_98", "civic_r_98", "s2000_a_99", "rx7_r_eu_93", "supra_rz_97", "lancer_evo8_gsr_03"};
    db::SpecDb db = sim::ai::open_specdb(disc);
    const sim::Program program = sim::Program::load(disc);
    std::printf("#course\tbox\taids\tlaps\tlap_s\ttop_kmh\tstuck_s\twalls\tcar_hits\tshifts\tplace\tspins\tslide_s\tflags\n");
    for (const std::string& course : courses) {
        std::shared_ptr<const course::Runway> road;
        try {
            road = load_road(disc, course);
        } catch (const std::exception& e) {
            std::printf("%s\t-\t-\t0\t-\t0\t0\t0\t0\t0\t-\tERROR %s\n", course.c_str(), e.what());
            continue;
        }
        for (int manual = 0; manual < 2; ++manual)
            for (int aids = 1; aids >= 0; --aids) {
                std::string flags, times;
                int done = 0, walls = 0, cars_hit = 0, shifts = 0, place = 0, spins = 0;
                double top = 0, stuck = 0, slide = 0;
                bool spinning = false;
                try {
                    std::vector<sim::Entry> entries{size_t(ncars)};
                    for (int i = 0; i < ncars; ++i)
                        if (!sim::stock_car(db, program, i == 0 ? car : field_labels[size_t(i - 1) % field_labels.size()], entries[size_t(i)].equipment))
                            throw std::runtime_error("no car");
                    entries[0].automatic = !manual;
                    entries[0].asm_on = entries[0].tcs_on = aids != 0;
                    sim::Session session(db, program, road, entries, sim::course_row(db, course));
                    // (the player's own driver planned beside him, for the line the active steering aims along: race_world.cpp)
                    std::vector<size_t> computer;
                    for (int i = 0; i < ncars; ++i) computer.push_back(size_t(i));
                    std::unique_ptr<sim::ai::Field> field = sim::ai::Field::open(disc, course, session, computer, {});
                    field->set_shadow(0);
                    for (size_t c : computer)
                        if (c > 0) session.set_driver_aids(c, field->params(c).aids, field->params(c).grip);
                    sim::LineDriver pad(session, 0);
                    const sim::SpecFigures fig = sim::spec_figures(session.spec(0));
                    std::vector<race::CarInput> in{size_t(ncars)};
                    for (int k = 0; k < sim::Session::kHoldSteps; ++k) {
                        field->hold(in);
                        in[0] = {};
                        in[0].throttle = 0.6f;
                        session.hold_once(in);
                    }
                    int last_lap = session.cars()[0].lap, hold_shift = 0;
                    double lap_start = 0;
                    const double limit = std::max(300.0, double(road->length) / 8.0) * laps + 60.0;
                    for (int step = 0; step < int(limit * 60) && done < laps; ++step) {
                        field->drive(in);
                        field->apply_time_scales(session);
                        if (line) session.set_line_heading(0, field->line_heading_at(0), field->line_mode_at(0));
                        const race::CarState& me = session.cars()[0];
                        race::CarInput p = pad.drive(1.0f, true);
                        if (manual && hold_shift <= 0) {
                            if (me.engine_rpm > float(fig.rev_limit_rpm - 300) && me.gear < session.spec(0).gears()) p.shift_up = true, hold_shift = 20, ++shifts;
                            else if (me.engine_rpm < 0.55f * float(fig.power_rpm) && me.gear > 1) p.shift_down = true, hold_shift = 20, ++shifts;
                        }
                        --hold_shift;
                        in[0] = p;
                        session.step_once(in);
                        const race::CarState& s = session.cars()[0];
                        if (!finite_state(s)) {
                            flags += "NAN ";
                            break;
                        }
                        const double t = session.time();
                        top = std::max(top, std::abs(double(s.speed)) * 3.6);
                        if (t > 5 && std::abs(s.speed) * 3.6f < 5) stuck += 1.0 / 60;
                        if (s.contact > 0 && s.contact_kind == 1) ++walls;
                        if (s.contact > 0 && s.contact_kind == 2) ++cars_hit;
                        // The slip angle between the body's heading and its course over the ground (level): a slide over
                        // 15 degrees counts its time, a spin begins past 80 and ends under 30.
                        if (const float v = std::hypot(s.velocity.x, s.velocity.z); v > 3.0f) {
                            const float along = (s.velocity.x * s.forward.x + s.velocity.z * s.forward.z) / v;
                            const float across = (s.velocity.x * s.right.x + s.velocity.z * s.right.z) / v;
                            const float slip = std::abs(std::atan2(across, along)) * 57.29578f;
                            if (slip > 15) slide += 1.0 / 60;
                            if (!spinning && slip > 80) spinning = true, ++spins;
                            else if (spinning && slip < 30) spinning = false;
                        }
                        if (s.lap > last_lap) {
                            if (last_lap >= 0) {
                                char b[16];
                                std::snprintf(b, sizeof b, "%s%.3f", times.empty() ? "" : ",", t - lap_start);
                                times += b, ++done;
                            }
                            lap_start = t;
                        }
                        last_lap = s.lap;
                    }
                    // The place: cars further along (laps, then distance).
                    const auto progress = [&](size_t i) { return double(session.cars()[i].lap) * 1e6 + double(session.cars()[i].course_v); };
                    place = 1;
                    for (int i = 1; i < ncars; ++i) place += progress(size_t(i)) > progress(0);
                    if (done < laps) flags += "UNFINISHED ";
                    if (stuck > 5) flags += "STUCK ";
                } catch (const std::exception& e) {
                    flags += std::string("ERROR ") + e.what() + " ";
                }
                if (!flags.empty()) flags.pop_back();
                std::printf("%s\t%s\t%s\t%d\t%s\t%.0f\t%.1f\t%d\t%d\t%d\t%d\t%d\t%.1f\t%s\n", course.c_str(), manual ? "MT" : "AT", aids ? "on" : "off", done,
                            times.empty() ? "-" : times.c_str(), top, stuck, walls, cars_hit, shifts, place, spins, slide, flags.empty() ? "-" : flags.c_str());
                std::fflush(stdout);
            }
    }
    return 0;
}

// A wall hit and what follows under an event's rules (event=<label>: its RACE row's hit penalty; none: no rules): the
// player's car alone drives `run` s, steers into the wall on the right at full throttle until a contact (or 20 s), then
// drives on along the road at full throttle for `after` s. The strongest contact, the penalty's seconds as the session
// shows them and the speed, each half second.
int sweep_wall(Disc& disc, const Args& a) {
    const std::string course = option_str(a, "course", "tsukuba"), car = option_str(a, "car", "integra_r_98"), label = option_str(a, "event", "");
    const double run = option(a, "run", 6), after = option(a, "after", 10);
    db::SpecDb db = sim::ai::open_specdb(disc);
    const sim::Program program = sim::Program::load(disc);
    std::shared_ptr<const course::Runway> road = load_road(disc, course);
    std::vector<sim::Entry> entries{1};
    if (!sim::stock_car(db, program, car, entries[0].equipment)) return std::fprintf(stderr, "no car %s\n", car.c_str()), 1;
    sim::Session session(db, program, road, entries);
    if (!label.empty()) {
        const db::RaceEvents events(db, [&disc](const std::string& path) { const gt3r::Volume::Entry* f = disc.volume().find(path); return f ? disc.volume().read(*f) : Bytes{}; });
        const std::optional<db::RaceEvent> e = events.event(label);
        if (!e) return std::fprintf(stderr, "no event %s\n", label.c_str()), 1;
        const sim::RaceRules r = sim::race_rules(e->bytes, program);
        std::printf("# %s: wall limit %d, car limit %d, penalty %d s, cap %d km/h\n", label.c_str(), int(r.hits.wall_limit), int(r.hits.car_limit), int(r.hits.penalty), int(r.penalty_cap));
        session.set_race_rules(r.hits, r.speed_cap, r.penalty_cap);
    }
    sim::LineDriver pad(session, 0);
    std::vector<race::CarInput> in{1};
    for (int k = 0; k < sim::Session::kHoldSteps; ++k) session.hold_once(in);
    for (int k = 0; k < int(run * 60); ++k) in[0] = pad.drive(1.0f, true), session.step_once(in);
    float worst = 0;
    int hit_at = -1;
    for (int k = 0; k < 20 * 60 && hit_at < 0; ++k) {
        in[0] = {}, in[0].throttle = 1, in[0].steer = float(option(a, "steer", 1));
        session.step_once(in);
        if (session.cars()[0].contact > 0) hit_at = k;
    }
    for (int k = 0; k < int(after * 60); ++k) {
        in[0] = pad.drive(1.0f, false);
        session.step_once(in);
        const race::CarState& s = session.cars()[0];
        if (s.contact > 0) worst = std::max(worst, s.contact);
        if (k % 30 == 29) std::printf("%.1f s  %.1f km/h  penalty %d s  worst contact %.2f\n", double(k + 1) / 60, double(s.speed) * 3.6, session.penalty_seconds(0), double(worst));
    }
    std::printf("# hit %s\n", hit_at < 0 ? "none" : "yes");
    return 0;
}

// Where every course's grid stands: each place's distance along the course (metres; past half the lap: behind the line),
// the session's lap there, and how many of the places are past the line.
int sweep_grid(Disc& disc, const Args& a) {
    db::SpecDb db = sim::ai::open_specdb(disc);
    const sim::Program program = sim::Program::load(disc);
    const int ncars = std::clamp(int(option(a, "cars", 6)), 1, 16);
    const std::string wanted = filter_of(a);
    std::printf("#course\tlap_m\tcars\tpast_line\tplaces_m\n");
    for (const std::string& name : disc.volume().list_dir("crs/")) {
        if (name.find('.') != std::string::npos || name.find('/') != std::string::npos) continue;
        if (!wanted.empty() && name.find(wanted) == std::string::npos) continue;
        try {
            std::shared_ptr<const course::Runway> road = load_road(disc, name);
            std::vector<sim::Entry> entries{size_t(ncars)};
            for (sim::Entry& e : entries)
                if (!sim::stock_car(db, program, option_str(a, "car", "nsx_r_02"), e.equipment)) throw std::runtime_error("no car");
            sim::Session session(db, program, road, entries);
            std::vector<race::CarInput> in{size_t(ncars)};
            session.hold_once(in);
            std::string places;
            int past = 0;
            for (const race::CarState& c : session.cars()) {
                const bool behind = c.course_v > road->length * 0.5f;
                past += !behind;
                char b[32];
                std::snprintf(b, sizeof b, "%s%.1f/%d", places.empty() ? "" : ",", double(behind ? c.course_v - road->length : c.course_v), c.lap);
                places += b;
            }
            std::printf("%s\t%.0f\t%d\t%d\t%s\n", name.c_str(), double(road->length), ncars, past, places.c_str());
        } catch (const std::exception& e) {
            std::printf("%s\t-\t-\t-\t%s\n", name.c_str(), e.what());
        }
        std::fflush(stdout);
    }
    return 0;
}

// A stop on the brake and a launch, the player's car alone through the pad's path: driven `run` s, braked to a stop
// (the pad's brake full), stood `idle` s (the brake held, or released with hold=0), then full throttle `launch` s.
// A line every quarter second: phase, time, gear, speed km/h and mph, rpm, the record's reverse request.
int sweep_stop(Disc& disc, const Args& a) {
    const std::string course = option_str(a, "course", "tsukuba");
    const std::string car = option_str(a, "car", "ka_01");
    const double run = option(a, "run", 8), idle = option(a, "idle", 4), launch = option(a, "launch", 8);
    const bool hold = option(a, "hold", 1) != 0, manual = option(a, "manual", 0) != 0, aids = option(a, "aids", 1) != 0;
    db::SpecDb db = sim::ai::open_specdb(disc);
    const sim::Program program = sim::Program::load(disc);
    std::shared_ptr<const course::Runway> road = load_road(disc, course);
    std::vector<sim::Entry> entries{1};
    if (!sim::stock_car(db, program, car, entries[0].equipment)) {
        std::fprintf(stderr, "no car %s\n", car.c_str());
        return 1;
    }
    entries[0].automatic = !manual;
    entries[0].asm_on = entries[0].tcs_on = aids;
    sim::Session session(db, program, road, entries);
    sim::LineDriver pad(session, 0);
    std::vector<race::CarInput> in{1};
    for (int k = 0; k < sim::Session::kHoldSteps; ++k) session.hold_once(in);
    std::printf("#phase\tt_s\tgear\tkmh\tmph\trpm\treverse_req\n");
    const auto line = [&](const char* phase, double t) {
        const race::CarState& s = session.cars()[0];
        std::printf("%s\t%.2f\t%d\t%.1f\t%.1f\t%.0f\t%d\n", phase, t, s.gear, double(s.speed) * 3.6, double(s.speed) * 2.23694, double(s.engine_rpm),
                    int(session.record(0).reverse_req));
    };
    int step = 0;
    const auto go = [&](const char* phase, double seconds, auto input, bool until_stopped) {
        int still = 0;
        for (int k = 0; k < int(seconds * 60); ++k, ++step) {
            in[0] = input();
            session.step_once(in);
            if (k % 15 == 14) line(phase, double(k + 1) / 60);
            if (until_stopped) {
                still = std::abs(session.cars()[0].speed) < 0.05f ? still + 1 : 0;
                if (still > 30) break;
            }
        }
    };
    go("run", run, [&] { return pad.drive(1.0f, true); }, false);
    go("brake", 30, [&] { race::CarInput p = pad.drive(0.0f, false); p.throttle = 0, p.brake = 1; return p; }, true);
    go("idle", idle, [&] { race::CarInput p{}; p.brake = hold ? 1.0f : 0.0f; return p; }, false);
    go("launch", launch, [&] { race::CarInput p = pad.drive(1.0f, false); p.throttle = 1, p.brake = 0; return p; }, false);
    return 0;
}

} // namespace

int cmd_sweep(Disc& disc, const std::vector<std::string>& a) {
    if (a[0] == "stop") return sweep_stop(disc, a);
    if (a[0] == "grid") return sweep_grid(disc, a);
    if (a[0] == "wall") return sweep_wall(disc, a);
    if (a.empty()) return 2;
    if (a[0] == "courses") return sweep_courses(disc, a);
    if (a[0] == "cars") return sweep_cars(disc, a);
    if (a[0] == "pad") return sweep_pad(disc, a);
    std::fprintf(stderr, "sweep: courses | cars\n");
    return 2;
}

} // namespace gt4r::tool
