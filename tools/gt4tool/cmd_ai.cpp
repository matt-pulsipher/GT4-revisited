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
//   gt4tool ai <disc> race <label> [laps] [seed] [stock] [rolling=<km/h>]
//                                              an event's drawn field run on its course at its pace, each opponent
//                                              with its pool entry's parts and settings ([stock]: as sold)
//   gt4tool ai <disc> entrant <enemy label>    an ENEMY_CARS row: its car, parts and settings against the car as sold
//   gt4tool ai <disc> entrants                 every ENEMY_CARS row so
//   gt4tool ai <disc> equipment <label>        the equipment record of a car as sold or of an ENEMY_CARS row, in hex
//   gt4tool ai <disc> demo <label|all> [frames] a demonstration run (replay/license/<label>): its race header, entries
//                                              and controls; [frames] every change of the pad record
//   gt4tool ai <disc> demo-run <label> [hold=480] the demonstration played on our session (sim::ai::DemoRun): car 0's
//                                              distance and speed, and where it comes to rest after braking
//
// Options anywhere: bspec=<car>[,<pace 0 .. 4>[,<corner>,<brake>,<alongside>]] drives that car by the B-spec driver (an empty
// record's skill, `flag`: on a course whose marks count; or the multipliers given); rules (with `race`) runs the event's fuel and tire wear and its class grips;
// `player` makes the last label entry 0 at the last place, as the game's player; tires=<kind> fits both axles' tires of a kind (0 .. 12) to a field given by labels; rolling=<km/h> starts the field moving (Entry::start_speed), trace=<car> prints a car's plan.

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/sim/ai/bspec_skill.h"
#include "gt4r/sim/ai/demo_driver.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/pit_tires.h"
#include "gt4r/sim/rules_wear.h"
#include "gt4r/sim/session.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <memory>
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

// A named option ("rolling=80") among the arguments; `def` without it.
double option(const Args& a, const char* name, double def) {
    const std::string key = std::string(name) + "=";
    for (const std::string& x : a)
        if (x.rfind(key, 0) == 0) return std::atof(x.c_str() + key.size());
    return def;
}

bool flag(const Args& a, const char* name) {
    for (const std::string& x : a)
        if (x == name) return true;
    return false;
}

// The option bspec=: the car driven by the B-spec driver, its pace and skill set before the first step.
std::function<void(sim::Session&, sim::ai::Field&)> bspec_control(const Args& args, const sim::Program& program) {
    std::string v;
    for (const std::string& a : args)
        if (a.rfind("bspec=", 0) == 0) v = a.substr(6);
    if (v.empty()) return {};
    const std::vector<std::string> p = split(v);
    const size_t car = size_t(std::atoi(p[0].c_str()));
    const int pace = p.size() > 1 ? std::atoi(p[1].c_str()) : 2;
    std::array<float, 3> given{};
    const bool own = p.size() > 4;
    for (size_t k = 0; own && k < 3; ++k) given[k] = float(std::atof(p[2 + k].c_str()));
    const sim::ai::BSpecTables tables = sim::ai::BSpecTables::load(program);
    auto done = std::make_shared<bool>(false);
    return [=](sim::Session& s, sim::ai::Field& f) {
        if (!*done && car < s.cars().size()) {
            *done = true;
            f.set_bspec(car);
            const std::array<u8, 0x40> empty{};
            f.set_bspec_record(car, empty, sim::ai::bspec_class(gt3r::ee::F(s.rating(car))));
            if (own) f.set_bspec_skill(car, gt3r::ee::F(given[0]), gt3r::ee::F(given[1]), gt3r::ee::F(given[2]));
            else if (const auto sk = sim::ai::bspec_skill(tables, sim::ai::BSpecRecord::from_bytes(empty.data()), gt3r::ee::F(s.rating(car)), flag(args, "flag"))) {
                f.set_bspec_skill(car, sk->corner, sk->brake, sk->alongside);
                std::printf("bspec car %zu: skill %.4f %.4f %.4f\n", car, double(sk->corner.host()), double(sk->brake.host()), double(sk->alongside.host()));
            }
            f.command(car, {pace, -1});
        }
        if (car < s.cars().size()) s.set_shift_factor(car, f.shift_factor(car));
    };
}

// The positional arguments (options left out).
Args positional(const Args& a) {
    Args out;
    for (const std::string& x : a)
        if (x.find('=') == std::string::npos && x != "stock") out.push_back(x);
    return out;
}

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

int entrant(Disc& disc, const Args& a) {
    db::SpecDb sdb = open_db(disc);
    const db::RaceEvents ev(sdb, reader(disc));
    const db::Labels* l = sdb.labels("ENEMY_CARS");
    const std::optional<u32> id = l ? l->id(a[1]) : std::nullopt;
    const std::optional<db::Opponent> o = id ? ev.opponent(*id) : std::nullopt;
    if (!o) {
        std::printf("no ENEMY_CARS row %s\n", a[1].c_str());
        return 1;
    }
    const sim::Program program = sim::Program::load(disc);
    sim::Equipment mine, sold;
    if (!sim::ai::opponent_equipment(sdb, program, *o, mine) || !sim::stock_car(sdb, program, o->car, sold)) {
        std::printf("%s: the car cannot be built\n", o->enemy.c_str());
        return 1;
    }
    std::printf("%s: car %s, parts %s, settings %s\n", o->enemy.c_str(), o->car.c_str(), o->parts.c_str(), o->param.c_str());
    for (u32 slot = 0x20; slot <= 0x108; slot += 8)
        if (mine.part(slot) != sold.part(slot)) {
            auto name = [&](db::PartId p) {
                if (p == db::kNoPart) return std::string("none");
                const char* t = db::part_table_name(db::part_table(p));
                const db::Labels* pl = t ? sdb.labels(t) : nullptr;
                const std::string* s = pl ? pl->label(db::part_row_id(p)) : nullptr;
                return s ? *s : std::to_string(db::part_row_id(p));
            };
            std::printf("  part +0x%02X: %s (as sold %s)\n", slot, name(mine.part(slot)).c_str(), name(sold.part(slot)).c_str());
        }
    for (u32 o2 = 0x110; o2 < sim::Equipment::kSize; ++o2)
        if (mine.bytes[o2] != sold.bytes[o2]) std::printf("  setting +0x%03X: %d (as sold %d)\n", o2, mine.bytes[o2], sold.bytes[o2]);
    sim::Spec a1, b1;
    sim::build_spec(sdb, program, mine, 0, a1);
    sim::build_spec(sdb, program, sold, 0, b1);
    const sim::SpecFigures fa = sim::spec_figures(a1), fb = sim::spec_figures(b1);
    std::printf("  %.0f PS at %d rpm, %d kg (as sold %.0f PS, %d kg)\n", double(fa.ps), fa.power_rpm, a1.weight_kg(), double(fb.ps), b1.weight_kg());
    return 0;
}

// Every ENEMY_CARS row against its car as sold: parts and settings that differ, power and weight.
int entrants(Disc& disc) {
    db::SpecDb sdb = open_db(disc);
    const db::RaceEvents ev(sdb, reader(disc));
    const db::Labels* l = sdb.labels("ENEMY_CARS");
    if (!l) return 1;
    const sim::Program program = sim::Program::load(disc);
    int rows = 0, built = 0, parts_differ = 0, settings_differ = 0, power_differ = 0, weight_differ = 0;
    for (const auto& [label, id] : l->entries) {
        const std::optional<db::Opponent> o = ev.opponent(id);
        if (!o) continue;
        ++rows;
        sim::Equipment mine, sold;
        if (!sim::ai::opponent_equipment(sdb, program, *o, mine) || !sim::stock_car(sdb, program, o->car, sold)) {
            std::printf("%-40s cannot be built\n", label.c_str());
            continue;
        }
        ++built;
        int np = 0, ns = 0;
        for (u32 slot = 0x20; slot <= 0x108; slot += 8) np += mine.part(slot) != sold.part(slot);
        for (u32 k = 0x110; k < sim::Equipment::kSize; ++k)
            if (k != 0x15E && k != 0x15F && k != 0x157 && mine.bytes[k] != sold.bytes[k]) ++ns;
        sim::Spec a1, b1;
        sim::build_spec(sdb, program, mine, 0, a1);
        sim::build_spec(sdb, program, sold, 0, b1);
        const sim::SpecFigures fa = sim::spec_figures(a1), fb = sim::spec_figures(b1);
        parts_differ += np > 0, settings_differ += ns > 0;
        power_differ += std::fabs(fa.ps - fb.ps) > 0.5f, weight_differ += a1.weight_kg() != b1.weight_kg();
        std::printf("%-40s %-28s parts %2d settings %2d  %4.0f PS %4d kg  (as sold %4.0f PS %4d kg)  ascc %d output %d\n", label.c_str(), o->car.c_str(), np, ns,
                    double(fa.ps), a1.weight_kg(), double(fb.ps), b1.weight_kg(), mine.b(0x157), mine.h(0x15E));
    }
    std::printf("%d rows, %d built; parts differ on %d, settings on %d, power on %d, weight on %d\n", rows, built, parts_differ, settings_differ, power_differ,
                weight_differ);
    return 0;
}

// A demonstration run's header, entries and controls (replay/license/<label>; "all": every file there, one line each).
int demo(Disc& disc, const Args& a) {
    const sim::Program program = sim::Program::load(disc);
    db::SpecDb sdb = open_db(disc);
    gt3r::Volume& vol = disc.volume();
    std::vector<std::string> labels;
    if (a[1] == "all") {
        for (const std::string& n : vol.list_dir("replay/license")) labels.push_back(n);
    } else {
        labels.push_back(a[1]);
    }
    const bool frames = a.size() > 2 && a[2] == "frames";
    int bad = 0;
    for (const std::string& label : labels) {
        const gt3r::Volume::Entry* e = vol.find(replay::demonstration_path(label));
        if (!e) {
            std::printf("%s: no file\n", label.c_str());
            ++bad;
            continue;
        }
        const Bytes file = vol.read(*e);
        std::string err;
        const auto d = replay::Demonstration::parse(file, program, &err);
        if (!d) {
            std::printf("%s: %s\n", label.c_str(), err.c_str());
            ++bad;
            continue;
        }
        auto car_label = [&](const sim::Equipment& eq) {
            const db::Labels* l = sdb.labels("GENERIC_CAR");
            const std::string* s = l ? l->label(u32(u64(eq.part(sim::Equipment::Car)))) : nullptr;
            return s ? *s : std::string("?");
        };
        std::printf("%-12s %-12s mode %u time %d.%03d s (%d) codes %llx %llx entries %zu", label.c_str(), d->race_class.c_str(), d->race_mode,
                    d->time_ms / 1000, d->time_ms % 1000, d->time2_ms, (unsigned long long)d->code_b0, (unsigned long long)d->code_b8, d->entries.size());
        for (const replay::DemoEntry& en : d->entries)
            std::printf(" [%s grid %d pedal %u kind %u aids %u%u%u%u]", car_label(en.equipment).c_str(), en.grid_place, en.pedal_style, en.driver_kind,
                        en.aids[0], en.aids[1], en.aids[2], en.aids[3]);
        for (const replay::ControlStream& s : d->streams) {
            std::printf(" stream %u frames", s.frame_count);
            if (!s.frames.empty()) std::printf(" kind %u", s.frames.back().kind());
        }
        std::printf("\n");
        if (!frames) continue;
        for (const replay::ControlStream& s : d->streams) {
            const replay::PadRecord* last = nullptr;
            for (size_t i = 0; i < s.frames.size(); ++i) {
                const replay::PadRecord& r = s.frames[i];
                if (last && *last == r) continue;
                std::printf("  %5zu flags %02x kind %u shift %d steer %d throttle %d brake %d axes %d %d b7 %u buttons", i, r.flags(), r.kind(), r.shift(),
                            r.steer(), r.throttle(), r.brake(), r.axis_c(), r.axis_e(), r.bytes[7]);
                for (u32 k = 0; k < r.bytes[3] && k < 16; ++k) std::printf(" %02x", r.bytes[0x10 + k]);
                std::printf("\n");
                last = &r;
            }
        }
    }
    return bad ? 1 : 0;
}

// A demonstration played on our session: the file's entries on the event's course, the start's hold (a license test's
// type-1 start: 8 s), then car 0 given the recording step by step.
int demo_run(Disc& disc, const Args& a, int hold) {
    const sim::Program program = sim::Program::load(disc);
    std::string err;
    const auto run = sim::ai::DemoRun::load(disc, program, a[1], &err);
    if (!run) {
        std::printf("%s: %s\n", a[1].c_str(), err.c_str());
        return 1;
    }
    db::SpecDb sdb = open_db(disc);
    const db::RaceEvents ev(sdb, reader(disc));
    auto event = ev.event(a[1]);
    if (!event) event = ev.event(u32(run->demo.code_b0));
    if (!event) {
        std::printf("%s: no event\n", a[1].c_str());
        return 1;
    }
    gt3r::Volume* vol = &disc.volume();
    const gt3r::Volume::Entry* file = vol->find("crs/" + event->course);
    if (!file) return 1;
    const Bytes data = vol->read(*file);
    const course::CourseFile parsed = course::CourseFile::parse(data);
    std::optional<course::Runway> road = course::course_road(parsed, data);
    if (!road) return 1;
    std::vector<sim::Entry> entries = run->entries();
    entries.resize(1); // (car 0: the recording's)
    sim::Session session(sdb, program, std::make_shared<const course::Runway>(std::move(*road)), entries, sim::course_row(sdb, event->course));
    std::vector<race::CarInput> in(1);
    const race::CarState& c = session.cars()[0];
    const course::Vec3 start = c.position;
    bool braked = false;
    int rest = -1;
    const int steps = int(run->steps()) + 120;
    for (int i = 0; i < steps; ++i) {
        in[0] = run->input(size_t(i));
        if (i < hold) session.hold_once(in);
        else session.step_once(in);
        const float d = std::hypot(c.position.x - start.x, c.position.z - start.z);
        if (run->record(size_t(i)).brake() > 0) braked = true;
        if (braked && rest < 0 && std::fabs(c.speed) < 0.01f) rest = i;
        if (i % 60 == 0 || i == rest)
            std::printf("  %5d  %8.3f m  %7.2f km/h  gear %d  throttle %.3f brake %.3f%s\n", i, double(d), double(c.speed) * 3.6, c.gear, double(in[0].throttle),
                        double(in[0].brake), i == rest ? "  at rest" : "");
    }
    std::printf("%s on %s: recorded %d ms; at rest after braking at step %d (%.3f s after the hold)\n", a[1].c_str(), event->course.c_str(),
                run->recorded_ms(), rest, rest >= 0 ? (rest - hold) / 60.0 : 0.0);
    return 0;
}

} // namespace

int cmd_ai(Disc& disc, const Args& args) {
    if (args.empty()) return 2;
    const Args a = positional(args);
    const float rolling = float(option(args, "rolling", 0) / 3.6);
    if (a[0] == "entrant" && a.size() > 1) return entrant(disc, a);
    if (a[0] == "entrants") return entrants(disc);
    if (a[0] == "equipment" && a.size() > 1) { // the equipment record's bytes of a car as sold or an ENEMY_CARS row
        db::SpecDb sdb = open_db(disc);
        const db::RaceEvents ev(sdb, reader(disc));
        const sim::Program program = sim::Program::load(disc);
        sim::ai::GridEntry g;
        g.car = a[1];
        if (const db::Labels* l = sdb.labels("ENEMY_CARS"); l && l->id(a[1])) g.enemy = a[1];
        sim::Equipment eq;
        if (!sim::ai::entry_equipment(sdb, program, ev, g, eq)) return 1;
        for (u32 k = 0; k < sim::Equipment::kSize; ++k) std::printf("%02x%s", eq.bytes[k], k % 16 == 15 ? "\n" : " ");
        std::printf("\n");
        return 0;
    }
    if (a[0] == "survey") return survey(disc);
    if (a[0] == "demo" && a.size() > 1) return demo(disc, a);
    if (a[0] == "demo-run" && a.size() > 1) return demo_run(disc, a, int(option(args, "hold", 480)));
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
        const sim::Program program = sim::Program::load(disc);
        std::vector<sim::ai::FieldCar> cars;
        for (const auto& g : grid) {
            sim::ai::FieldCar c;
            c.name = g.enemy.empty() ? g.car : g.enemy;
            sim::ai::GridEntry as = g;
            if (flag(args, "stock")) as.enemy.clear();
            if (!sim::ai::entry_equipment(sdb, program, ev, as, c.entry.equipment)) {
                std::printf("%s: cannot be built\n", c.name.c_str());
                return 1;
            }
            c.entry.start_speed = rolling;
            cars.push_back(c);
        }
        const int laps = a.size() > 2 ? std::max(1, std::atoi(a[2].c_str())) : std::max(1, e->laps);
        print_event(*e);
        if (flag(args, "rules"))
            for (sim::ai::FieldCar& c : cars) c.entry.fuel = e->bytes.size() > 0x43 && e->bytes[0x43] != 0;
        auto control = bspec_control(args, program);
        bool set = !flag(args, "rules");
        auto with_rules = [&](sim::Session& s, sim::ai::Field& f) {
            if (!set && e->bytes.size() >= 0x74) {
                u8 rule_block[0x80] = {};
                sim::rules::event_wear_bytes(e->bytes.data(), rule_block);
                s.set_wear_rules(rule_block, !s.loose(), program);
            }
            set = true;
            if (control) control(s, f);
        };
        const sim::ai::FieldRun run = sim::ai::run_field(disc, e->course, cars, laps, sim::ai::pace_for(*e), 0, {}, with_rules);
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
    trace = int(option(args, "trace", -1));
    auto on_step = [&](const sim::Session& s, const sim::ai::Field& f) {
        if (trace < 0 || size_t(trace) >= s.cars().size() || std::lround(s.time() * 60) % 30 != 0) return;
        const race::CarState& st = s.cars()[size_t(trace)];
        const sim::ai::Field::Snapshot o = f.snapshot(size_t(trace));
        std::printf("%7.2f s %7.1f lap %d  %5.1f km/h  line %d next %2d mode %d status %d state %d  cur %5.1f next %5.1f  thr %.2f brk %.2f steer %+.3f off %+5.2f contact %.2f follow %d lock %.3f cmd %+.3f\n",
                    s.time(), double(st.course_v), st.lap, double(st.speed) * 3.6, o.line, o.next, o.mode, o.status, o.state, double(o.v_cur) * 3.6,
                    double(o.v_next) * 3.6, double(o.throttle), double(o.brake), double(o.steer), double(o.line_offset), double(o.contact), o.follow, double(o.lock), double(o.steer_cmd));
    };
    std::vector<sim::ai::FieldCar> entries;
    {
        db::SpecDb sdb = open_db(disc);
        const sim::Program program = sim::Program::load(disc);
        for (const std::string& c : cars) {
            sim::ai::FieldCar f;
            f.name = c;
            if (!sim::stock_car(sdb, program, c, f.entry.equipment)) {
                std::printf("no car %s\n", c.c_str());
                return 1;
            }
            f.entry.start_speed = rolling;
            if (const int kind = int(option(args, "tires", -1)); kind >= 0) { // (both axles' tires of that kind from the car's parts file)
                gt3r::Volume* vol = &disc.volume();
                const auto read = [vol](const std::string& path) -> std::optional<Bytes> {
                    const gt3r::Volume::Entry* en = vol->find(path);
                    return en ? std::optional<Bytes>(vol->read(*en)) : std::nullopt;
                };
                const auto parts = sim::fitting_parts(read, sdb, f.entry.equipment.part(sim::Equipment::Car));
                for (int axle = 0; axle < 2; ++axle) {
                    const db::PartId id = sim::fitting_part(parts, u32(axle == 0 ? db::PartTable::FrontTire : db::PartTable::RearTire), u8(kind));
                    if (id != db::kNoPart) f.entry.equipment.set_part(axle == 0 ? sim::Equipment::FrontTire : sim::Equipment::RearTire, id);
                }
            }
            entries.push_back(f);
        }
        if (flag(args, "player")) { // (as a race of the game: the last car is entry 0, at the last place, the others before it)
            for (size_t i = 0; i < entries.size(); ++i) entries[i].entry.grid_place = int(i);
            std::rotate(entries.begin(), entries.end() - 1, entries.end());
        }
    }
    const sim::ai::FieldRun run = sim::ai::run_field(disc, a[0], entries, laps, {}, 0, on_step, bspec_control(args, sim::Program::load(disc)));
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
