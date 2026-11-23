// gt4tool's computer drivers' commands:
//
//   gt4tool ai <disc> <course> [cars] [laps]   a field of computer drivers run headless: each car's lap times and
//                                              incidents (wall and car contacts, spins, off the road). [cars]: a number
//                                              (of a default field) or labels joined by commas; [laps] 3.
//   gt4tool ai <disc> lines <course> [line]    the course's driver file: its lines as the race prepares them (with
//                                              [line] every point), its driver sets
//   gt4tool ai <disc> survey                   every course's driver file: its layout, lines and sets
//   gt4tool ai <disc> events                   the database's events: course, laps, grid, their pools' sizes
//   gt4tool ai <disc> event <label> [seed]     an event's columns, its pool of opponents and a grid drawn from it
//   gt4tool ai <disc> race <label> [laps] [seed]  an event's drawn field (cars as sold) run on its course at its pace

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/session.h"

#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace gt4r::tool {

namespace {

using Args = std::vector<std::string>;

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    size_t a = 0;
    while (a <= s.size()) {
        const size_t b = s.find(',', a);
        const std::string part = s.substr(a, b == std::string::npos ? std::string::npos : b - a);
        if (!part.empty()) out.push_back(part);
        if (b == std::string::npos) break;
        a = b + 1;
    }
    return out;
}

// A field of small cars of different classes (the grid of a Tsukuba race of the original).
const std::vector<std::string> kDefaultField = {"_1100_standard_type11_49", "z_act_70", "n360_67", "midget2_d_type_98", "s500_63", "ka_01",
                                                "civic_r_98", "integra_r_98", "nsx_r_02", "lancer_evo8_gsr_03"};

std::string clock(double s) {
    char b[32];
    const int m = int(s / 60);
    std::snprintf(b, sizeof b, "%d'%06.3f", m, s - 60.0 * m);
    return b;
}

int lines(Disc& disc, const Args& a) {
    const std::optional<course::AiData> data = sim::ai::Field::read(disc, a[1]);
    if (!data) {
        std::printf("no driver file crs/%s.ad\n", a[1].c_str());
        return 1;
    }
    std::printf("%s: %s points\n", a[1].c_str(), data->format == course::AiData::Format::Floats ? "ADLN (0x40-byte)" : "fixed point (0x28-byte)");
    for (int c = 0; c < course::AiData::kClasses; ++c) {
        std::printf("set %d:", c);
        if (!data->sets[size_t(c)]) std::printf(" none");
        else
            for (int i = 0; i < 0x30; ++i) std::printf(" %d", (*data->sets[size_t(c)])[size_t(i)]);
        std::printf("\n");
    }
    const std::vector<std::string> one = {"ka_01"};
    std::unique_ptr<sim::Session> session = sim::Session::open(disc, a[1], one);
    const sim::Program program = sim::Program::load(disc);
    const sim::ai::Tables tables = sim::ai::Tables::load(program);
    const sim::ai::CourseLines cl(*data, session->ground(), tables);
    std::printf("lap %.1f m\n", double(cl.lap()));
    const int only = a.size() > 2 ? std::atoi(a[2].c_str()) : -1;
    for (int k = 0; k < 8; ++k) {
        const auto& pts = cl.line(k);
        std::printf("line %d: %zu points (file %zu)", k, pts.size(), data->lines[size_t(k)].size());
        if (!pts.empty()) {
            // How well each stretch's end, evaluated, meets the next point.
            float worst = 0;
            for (size_t i = 0; i + 1 < pts.size(); ++i) {
                float x, y, h;
                if (cl.evaluate(k, int(i + 1), pts[i + 1].dist - 0.01f, x, y, h)) worst = std::max(worst, std::hypot(x - pts[i + 1].x, y - pts[i + 1].y));
            }
            std::printf(", distances %.1f .. %.1f, stretch ends within %.2f m", double(pts.front().dist), double(pts.back().dist), double(worst));
        }
        std::printf("\n");
        if (k == only)
            for (size_t i = 0; i < pts.size(); ++i) {
                const auto& p = pts[i];
                std::printf("  %3zu kind %d b %4d v %4d  x %8.2f y %8.2f h %6.2f  surf %2d%s dist %7.1f  r %8.2f  head %6.3f  slope %6.3f bank %6.3f  len %6.1f%s\n",
                            i, p.kind, p.brake_adjust, p.speed_adjust, double(p.x), double(p.y), double(p.height), p.surface, p.pit ? " pit" : "    ",
                            double(p.dist), double(p.radius), double(p.heading), double(p.slope), double(p.bank), double(p.length), p.crest ? " crest" : "");
            }
    }
    return 0;
}

int survey(Disc& disc) {
    int files = 0, fixed = 0;
    std::array<int, 8> present{};
    for (const std::string& name : disc.volume().list_dir("crs/")) {
        if (name.size() < 4 || name.substr(name.size() - 3) != ".ad") continue;
        const std::string course = name.substr(0, name.size() - 3);
        try {
            const std::optional<course::AiData> d = sim::ai::Field::read(disc, course);
            if (!d) {
                std::printf("%-24s not a driver file\n", course.c_str());
                continue;
            }
            ++files;
            if (d->format == course::AiData::Format::Fixed) ++fixed;
            std::printf("%-24s %s lines", course.c_str(), d->format == course::AiData::Format::Fixed ? "fixed " : "ADLN  ");
            for (int k = 0; k < 8; ++k) {
                std::printf(" %3zu", d->lines[size_t(k)].size());
                if (!d->lines[size_t(k)].empty()) ++present[size_t(k)];
            }
            int sets = 0;
            for (const auto& s : d->sets) sets += s ? 1 : 0;
            std::printf("  sets %d  class 0: brake %d corner %d look %d..%d\n", sets, d->sets[0] ? (*d->sets[0])[0] : 0, d->sets[0] ? (*d->sets[0])[1] : 0,
                        d->sets[0] ? (*d->sets[0])[7] : 0, d->sets[0] ? (*d->sets[0])[8] : 0);
        } catch (const std::exception& e) {
            std::printf("%-24s %s\n", course.c_str(), e.what());
        }
    }
    std::printf("%d files (%d fixed point); courses with line k:", files, fixed);
    for (int k = 0; k < 8; ++k) std::printf(" %d", present[size_t(k)]);
    std::printf("\n");
    return 0;
}

db::SpecDb open_db(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
}

db::SpecDb::Reader reader(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
}

void print_event(const db::RaceEvent& e) {
    std::printf("%s (row %u): course %s, %d laps, grid %d, x44 %d, time limit %d\n", e.label.c_str(), e.id, e.course.c_str(), e.laps, e.grid, e.x44,
                e.time_limit);
    std::printf("  targets %s %s %s  prizes", e.target_ms[0] ? clock(e.target_ms[0] / 1000.0).c_str() : "-",
                e.target_ms[1] ? clock(e.target_ms[1] / 1000.0).c_str() : "-", e.target_ms[2] ? clock(e.target_ms[2] / 1000.0).c_str() : "-");
    for (u16 p : e.prize) std::printf(" %u", p);
    std::printf(" (+0x2C %d)\n", e.prize_x2c);
    std::printf("  needs: license %d drivetrain %d aspiration %d category %d tires %d power %d PS price %d weight %d length %d country %d years %d..%d\n",
                e.need_license, e.need_drivetrain, e.need_aspiration, e.need_car_type, e.need_tire, e.need_ps, e.need_price, e.limit_weight, e.limit_length,
                e.need_country, e.year_from, e.year_to);
    std::printf("  drivers: throttle %d %d %d %d  braking %d %d %d %d  arcs %d %d %d %d  band %d mode %d slow %d%% %d..%d0 m fast %d%% %d..%d0 m\n",
                e.ai_throttle[0], e.ai_throttle[1], e.ai_throttle[2], e.ai_throttle[3], e.ai_brake[0], e.ai_brake[1], e.ai_brake[2], e.ai_brake[3],
                e.ai_corner[0], e.ai_corner[1], e.ai_corner[2], e.ai_corner[3], e.band_global, e.band_mode, e.slow_pct, e.slow_start, e.slow_full,
                e.fast_pct, e.fast_start, e.fast_full);
}

} // namespace

int cmd_ai(Disc& disc, const Args& a) {
    if (a.empty()) return 2;
    if (a[0] == "survey") return survey(disc);
    if (a[0] == "events" || a[0] == "event" || a[0] == "race") {
        db::SpecDb sdb = open_db(disc);
        const db::RaceEvents ev(sdb, reader(disc));
        if (a[0] == "events") {
            for (const std::string& l : ev.labels())
                if (const auto e = ev.event(l))
                    std::printf("%-28s %-20s laps %3d grid %2d pool %3zu  drivers %d/%d/%d band %d/%d\n", l.c_str(), e->course.c_str(), e->laps, e->grid,
                                ev.pool(*e).size(), e->ai_throttle[0], e->ai_brake[0], e->ai_corner[0], e->slow_pct, e->fast_pct);
            return 0;
        }
        if (a.size() < 2) return 2;
        const std::optional<db::RaceEvent> e = ev.event(a[1]);
        if (!e) {
            std::printf("no event %s\n", a[1].c_str());
            return 1;
        }
        menu::RaceRequest req;
        req.race = e->label;
        const size_t seed_at = a[0] == "race" ? 3 : 2;
        const u32 seed = a.size() > seed_at ? u32(std::strtoul(a[seed_at].c_str(), nullptr, 0)) : 1;
        const std::vector<sim::ai::GridEntry> grid = sim::ai::grid_for(ev, req, seed);
        if (a[0] == "event") {
            print_event(*e);
            const std::vector<db::Opponent> pool = ev.pool(*e);
            std::printf("  pool of %zu:\n", pool.size());
            for (const db::Opponent& o : pool) std::printf("    %-36s %-30s parts %-30s settings %s paint %d\n", o.enemy.c_str(), o.car.c_str(), o.parts.c_str(), o.param.c_str(), o.paint);
            std::printf("  drawn (seed %u):", seed);
            for (const auto& g : grid) std::printf(" %s", g.car.c_str());
            std::printf("\n");
            return 0;
        }
        std::vector<std::string> cars;
        for (const auto& g : grid) cars.push_back(g.car);
        const int laps = a.size() > 2 ? std::max(1, std::atoi(a[2].c_str())) : std::max(1, e->laps);
        print_event(*e);
        const sim::ai::FieldRun run = sim::ai::run_field(disc, e->course, cars, laps, sim::ai::pace_for(*e));
        std::printf("%s, lap %.1f m, %d laps, %.1f s run\n", run.course.c_str(), double(run.lap_length), laps, run.seconds);
        for (const sim::ai::CarRun& r : run.cars) {
            std::printf("%-28s %-10s", r.car.c_str(), r.finished ? clock(r.total).c_str() : "-");
            for (double l : r.laps) std::printf(" %s", clock(l).c_str());
            std::printf("  walls %d cars %d spins %d\n", r.incidents.wall_hits, r.incidents.car_hits, r.incidents.spins);
        }
        return 0;
    }
    if (a[0] == "lines" && a.size() > 1) return lines(disc, a);
    std::vector<std::string> cars;
    if (a.size() > 1) {
        const std::string& c = a[1];
        if (!c.empty() && c.find_first_not_of("0123456789") == std::string::npos) {
            const size_t n = std::min<size_t>(std::stoul(c), kDefaultField.size());
            cars.assign(kDefaultField.begin(), kDefaultField.begin() + long(n));
        } else cars = split(c);
    } else cars.assign(kDefaultField.begin(), kDefaultField.begin() + 6);
    const int laps = a.size() > 2 ? std::max(1, std::atoi(a[2].c_str())) : 3;
    // [trace=<car>]: that car's plan every half second.
    int trace = -1;
    for (const std::string& x : a)
        if (x.rfind("trace=", 0) == 0) trace = std::atoi(x.c_str() + 6);
    auto on_step = [&](const sim::Session& s, const sim::ai::Field& f) {
        if (trace < 0 || size_t(trace) >= s.cars().size() || std::lround(s.time() * 60) % 30 != 0) return;
        const race::CarState& st = s.cars()[size_t(trace)];
        const sim::ai::Field::Snapshot o = f.snapshot(size_t(trace));
        std::printf("%7.2f s %7.1f lap %d  %5.1f km/h  line %d next %2d mode %d status %d state %d  cur %5.1f next %5.1f  thr %.2f brk %.2f steer %+.3f off %+5.2f contact %.2f follow %d lock %.3f cmd %+.3f\n",
                    s.time(), double(st.course_v), st.lap, double(st.speed) * 3.6, o.line, o.next, o.mode, o.status, o.state, double(o.v_cur) * 3.6,
                    double(o.v_next) * 3.6, double(o.throttle), double(o.brake), double(o.steer), double(o.line_offset), double(o.contact), o.follow, double(o.lock), double(o.steer_cmd));
    };
    const sim::ai::FieldRun run = sim::ai::run_field(disc, a[0], cars, laps, {}, 0, on_step);
    for (const std::string& n : run.notes) std::printf("note: %s\n", n.c_str());
    std::printf("%s, lap %.1f m, %d laps, %.1f s run\n", run.course.c_str(), double(run.lap_length), laps, run.seconds);
    std::printf("%-28s %-10s %-10s %s  %5s %4s %4s %5s %5s %6s %s\n", "car", "total", "best", "laps", "top", "wall", "car", "spins", "off", "stuck", "lanes");
    for (const sim::ai::CarRun& r : run.cars) {
        double best = 0;
        for (double l : r.laps)
            if (best == 0 || l < best) best = l;
        std::printf("%-28s %-10s %-10s", r.car.c_str(), r.finished ? clock(r.total).c_str() : "-", best > 0 ? clock(best).c_str() : "-");
        for (double l : r.laps) std::printf(" %s", clock(l).c_str());
        const sim::ai::Incidents& i = r.incidents;
        std::printf("  %5.0f %4d %4d %5d %5d %6.1f %d\n", double(r.top_speed) * 3.6, i.wall_hits, i.car_hits, i.spins, i.off_road, double(i.stuck_seconds), i.lane_changes);
    }
    return 0;
}

} // namespace gt4r::tool
