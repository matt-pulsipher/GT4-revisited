// The computer drivers: the courses' driver files, the lines as the race prepares them, fields of
// drivers on a circuit, a city course and a dirt course, the events and their opponents.

#include "test_framework.h"

#include "gt4r/formats/course/ai_line.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/ai/run.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>

using namespace gt4r;
using gt3r::s8;

namespace {

void put32(Bytes& b, size_t at, u32 v) {
    if (b.size() < at + 4) b.resize(at + 4);
    std::memcpy(b.data() + at, &v, 4);
}
void putf(Bytes& b, size_t at, float v) {
    u32 u;
    std::memcpy(&u, &v, 4);
    put32(b, at, u);
}

Disc* gt4_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
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

// A made-up driver file: the table of 32 offsets, the lines' section at 0x80, four sets after it.
Bytes made_up_file(bool floats) {
    Bytes f(0x80, 0);
    put32(f, 4, 0x80);
    if (floats) {
        Bytes s(0x30, 0);
        std::memcpy(s.data(), "ADLN", 4);
        put32(s, 0x14, 8), put32(s, 0x18, 0x10);
        // Line 6: two points (a straight, then an arc of radius -20).
        put32(s, 0x10 + 4 * 6, 0x30);
        s.resize(0x30 + 0x40 + 2 * 0x40, 0);
        put32(s, 0x30, 2);
        size_t p = 0x70;
        s[p] = 0, s[p + 1] = u8(s8(-10)), s[p + 2] = u8(s8(-128));
        putf(s, p + 4, 1.5f), putf(s, p + 8, -2.5f), putf(s, p + 0x18, 0), putf(s, p + 0x24, 0.25f);
        s[p + 0x10] = 0xFF, s[p + 0x11] = 0xFF;
        p += 0x40;
        s[p] = 1, s[p + 2] = 5;
        putf(s, p + 4, 3), putf(s, p + 8, 4), putf(s, p + 0x18, -20), putf(s, p + 0x24, 1.0f);
        f.insert(f.end(), s.begin(), s.end());
    } else {
        Bytes s(0x28, 0);
        put32(s, 4, 8);
        for (int k = 0; k < 8; ++k) put32(s, 8 + 4 * size_t(k), 0x28);
        put32(s, 8 + 4 * 6, 0x2C); // line 6 after line 0's empty list
        s.resize(0x2C + 4 + 0x28, 0);
        put32(s, 0x2C, 1);
        const size_t p = 0x30;
        s[p] = 1, s[p + 1] = 20, s[p + 2] = u8(s8(-5));
        put32(s, p + 4, u32(s32(4096 * 10))), put32(s, p + 8, u32(s32(-4096 * 3))), put32(s, p + 0x18, u32(s32(4096 * 50)));
        put32(s, p + 0x24, 1024); // a quarter turn
        put32(s, 0, u32(s.size()));
        f.insert(f.end(), s.begin(), s.end());
    }
    for (int c = 0; c < 4; ++c) {
        put32(f, 4 * (2 + size_t(c)), u32(f.size()));
        Bytes set(0x80, 0);
        set[0] = u8(100 + c);
        f.insert(f.end(), set.begin(), set.end());
    }
    return f;
}

bool reasonable(const sim::ai::FieldRun& run, int laps, double max_stuck, std::string& why) {
    for (const sim::ai::CarRun& r : run.cars) {
        if (!r.finished || int(r.laps.size()) < laps) {
            why = r.car + " did not finish";
            return false;
        }
        if (r.incidents.stuck_seconds > max_stuck) {
            why = r.car + " stood still";
            return false;
        }
    }
    return true;
}

} // namespace

TEST(ai_file_floats) {
    const Bytes f = made_up_file(true);
    CHECK(course::AiData::looks_like(f));
    const course::AiData d = course::AiData::parse(f);
    CHECK(d.format == course::AiData::Format::Floats);
    CHECK(d.lines[6].size() == 2);
    CHECK(d.lines[1].empty());
    if (d.lines[6].size() == 2) {
        const course::AiLinePoint &a = d.lines[6][0], &b = d.lines[6][1];
        CHECK(a.kind == 0 && a.brake_adjust == -10 && a.speed_adjust == -128);
        CHECK(a.x == 1.5f && a.y == -2.5f && a.heading == 0.25f && a.segment == 0xFFFF);
        CHECK(b.kind == 1 && b.speed_adjust == 5 && b.radius == -20.0f);
    }
    for (int c = 0; c < 4; ++c) CHECK(d.sets[size_t(c)] && (*d.sets[size_t(c)])[0] == 100 + c);
}

TEST(ai_file_fixed) {
    const course::AiData d = course::AiData::parse(made_up_file(false));
    CHECK(d.format == course::AiData::Format::Fixed);
    CHECK(d.lines[6].size() == 1);
    if (!d.lines[6].empty()) {
        const course::AiLinePoint& p = d.lines[6][0];
        CHECK(p.kind == 1 && p.brake_adjust == 20 && p.speed_adjust == -5);
        CHECK(p.x == 10.0f && p.y == -3.0f && p.radius == 50.0f);
        CHECK(std::fabs(p.heading - 1.5707964f) < 1e-5f);
    }
}

TEST(ai_ramp_and_neutral_tables) {
    using gt3r::ee::F;
    const sim::ai::Ramp r{F(0.8f), F(1.0f), F(-0.12f), F(-0.04f)};
    CHECK(r.at(F(0.5f)).host() == -0.12f && r.at(F(2.0f)).host() == -0.04f && std::fabs(r.at(F(0.9f)).host() + 0.08f) < 1e-6f);
    const sim::ai::Tables t = sim::ai::Tables::load(sim::Program{});
    CHECK(!t.known);
    CHECK(t.fallback[1][0] == 1 && t.fallback[1][1] == 6 && t.fallback[6][0] == 6);
    CHECK(t.tuning.offset_flags == 0 && t.driver_spread == 0);
}

TEST(ai_disc_driver_files) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    int files = 0, fixed = 0, mains = 0;
    for (const std::string& name : disc->volume().list_dir("crs/")) {
        if (name.size() < 4 || name.substr(name.size() - 3) != ".ad") continue;
        const std::optional<course::AiData> d = sim::ai::Field::read(*disc, name.substr(0, name.size() - 3));
        CHECK(d.has_value());
        if (!d) continue;
        ++files;
        fixed += d->format == course::AiData::Format::Fixed;
        mains += !d->lines[6].empty();
        for (const auto& s : d->sets) CHECK(s.has_value());
    }
    CHECK(files == 110);
    CHECK(fixed == 17);
    CHECK(mains == 110);
}

TEST(ai_disc_lines_prepared) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    for (const char* name : {"tsukuba", "seattle", "grandcanyon", "nurburgring", "r_akasaka", "suzuka"}) {
        const std::vector<std::string> one = {"ka_01"};
        std::unique_ptr<sim::Session> s = sim::Session::open(*disc, name, one);
        const std::optional<course::AiData> d = sim::ai::Field::read(*disc, name);
        CHECK(d.has_value());
        if (!d) continue;
        const sim::ai::CourseLines cl(*d, s->ground(), sim::ai::Tables::load(sim::Program::load(*disc)));
        const auto& main = cl.line(6);
        CHECK(main.size() > 2);
        // The main line runs the lap: its distances rise through it, and each stretch, evaluated, ends on the next point.
        bool rising = true;
        float worst = 0;
        for (size_t i = 0; i + 1 < main.size(); ++i) {
            if (main[i + 1].dist <= main[i].dist) rising = false;
            float x, y, h;
            if (cl.evaluate(6, int(i + 1), main[i + 1].dist - 0.01f, x, y, h)) worst = std::max(worst, std::hypot(x - main[i + 1].x, y - main[i + 1].y));
        }
        CHECK(rising);
        CHECK(worst < 0.1f);
        // The points stand on the road (the file's frame is the simulation's turned half round).
        int on = 0;
        for (const auto& p : main) {
            sim::GroundHit h;
            if (s->ground().cast_down({p.x, p.height + 1.0f, -p.y}, h) && std::fabs(h.point.y - p.height) < 0.5f) ++on;
        }
        CHECK(on * 10 >= int(main.size()) * 9);
    }
}

TEST(ai_disc_fields_finish) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const std::vector<std::string> cars = {"_1100_standard_type11_49", "z_act_70", "n360_67", "midget2_d_type_98", "s500_63", "ka_01"};
    // A circuit, a city course and a dirt course: six cars finish three laps, none standing still on the paved courses.
    // On Grand Canyon's climbs the smallest cars crawl and stop as they do in the program's own race with its own drivers
    // (gt4r_diff ai grandcanyon: the Z, N360 and S500 below 5 km/h for 90 .. 140 s in three laps).
    for (const char* course : {"tsukuba", "seattle", "grandcanyon"}) {
        const sim::ai::FieldRun run = sim::ai::run_field(*disc, course, cars, 3);
        std::string why;
        const bool ok = reasonable(run, 3, std::string(course) == "grandcanyon" ? 240.0 : 10.0, why);
        if (!ok) std::printf("  %s: %s\n", course, why.c_str());
        CHECK(ok);
        int walls = 0;
        for (const auto& r : run.cars) walls += r.incidents.wall_hits;
        CHECK(walls < 60);
    }
}

TEST(ai_disc_lap_times_by_class) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // From a 25 PS saloon of 1949 to a 280 PS four wheel drive of 2003: each class laps faster.
    const std::vector<std::string> cars = {"_1100_standard_type11_49", "ka_01", "civic_r_98", "lancer_evo8_gsr_03"};
    std::vector<double> best;
    for (const std::string& car : cars) {
        const sim::ai::FieldRun run = sim::ai::run_field(*disc, "tsukuba", {car}, 2);
        double b = 0;
        for (double l : run.cars[0].laps)
            if (b == 0 || l < b) b = l;
        CHECK(run.cars[0].finished);
        best.push_back(b);
        std::printf("  tsukuba %-26s best %.3f s\n", car.c_str(), b);
    }
    for (size_t i = 0; i + 1 < best.size(); ++i) CHECK(best[i + 1] < best[i]);
    // Tsukuba's lap is 2,115 m: a modern sports car laps it in about a minute and ten.
    CHECK(best.back() > 55 && best.back() < 85);
}

TEST(ai_disc_tsukuba_reference) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The original's Tsukuba race of these five (a rolling start at 50 mph, two laps): the winner's total 3'04.462, the
    // fifth 17.6 s later, so laps of about 92 to 101 s. Ours start from rest: their best laps fall within 15 %.
    const std::vector<std::string> cars = {"_1100_standard_type11_49", "z_act_70", "n360_67", "midget2_d_type_98", "s500_63"};
    const sim::ai::FieldRun run = sim::ai::run_field(*disc, "tsukuba", cars, 2);
    for (const auto& r : run.cars) {
        double b = 0;
        for (double l : r.laps)
            if (b == 0 || l < b) b = l;
        std::printf("  %-26s best %.3f s\n", r.car.c_str(), b);
        CHECK(r.finished);
        CHECK(b > 92.0 * 0.85 && b < 101.0 * 1.15);
    }
}

TEST(ai_disc_events) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const db::RaceEvents ev(sdb, reader(*disc));
    const std::vector<std::string> labels = ev.labels();
    CHECK(labels.size() == 709);
    int pools = 0, opponents = 0, unresolved = 0;
    for (const std::string& l : labels) {
        const std::optional<db::RaceEvent> e = ev.event(l);
        CHECK(e.has_value());
        if (!e) continue;
        CHECK(!e->course.empty());
        CHECK(e->grid >= 0 && e->grid <= 32);
        const std::vector<db::Opponent> p = ev.pool(*e);
        if (!p.empty()) ++pools;
        for (const db::Opponent& o : p) {
            ++opponents;
            if (o.car.empty() || o.parts.empty()) ++unresolved;
        }
    }
    std::printf("  %zu events, %d with a pool, %d opponents, %d unresolved\n", labels.size(), pools, opponents, unresolved);
    CHECK(pools > 600);
    CHECK(unresolved == 0);
    // A Sunday Cup: two laps, six cars, five opponents drawn from its pool of five, each once.
    const std::optional<db::RaceEvent> e = ev.event("s_sunday_b_0001");
    CHECK(e && e->laps == 2 && e->grid == 6);
    if (e) {
        menu::RaceRequest req;
        req.race = e->label, req.car = "ka_01";
        const std::vector<sim::ai::GridEntry> grid = sim::ai::grid_for(ev, req, 7);
        CHECK(grid.size() == 6);
        std::set<std::string> seen;
        for (size_t i = 0; i + 1 < grid.size(); ++i) seen.insert(grid[i].enemy);
        CHECK(seen.size() == 5);
        CHECK(!grid.empty() && grid.back().player && grid.back().car == "ka_01");
    }
    // The license tests' events carry the rubber band (the races none).
    const std::optional<db::RaceEvent> lic = ev.event("usl0a0005");
    CHECK(lic && lic->slow_pct == 50);
}

TEST(ai_disc_opponent_equipment) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = open_db(*disc);
    const db::RaceEvents ev(sdb, reader(*disc));
    const sim::Program program = sim::Program::load(*disc);
    const db::Labels* l = sdb.labels("ENEMY_CARS");
    CHECK(l != nullptr);
    if (!l) return;
    // Every ENEMY_CARS row builds: its car with its own parts and settings.
    int rows = 0, built = 0;
    for (const auto& [label, id] : l->entries) {
        const std::optional<db::Opponent> o = ev.opponent(id);
        if (!o) continue;
        ++rows;
        sim::Equipment eq;
        if (sim::ai::opponent_equipment(sdb, program, *o, eq)) ++built;
    }
    CHECK(rows == 2558 && built == rows);
    // An entered car names its row (+0x08), takes the row's settings (the first ASCC index 1) and its output factor: the
    // BMW M3 '04 of the North American series at 1,150 per mille, about 400 PS against 344 as sold.
    const std::optional<u32> id = l->id("gt4r_na_m3_04");
    CHECK(id.has_value());
    if (!id) return;
    const std::optional<db::Opponent> o = ev.opponent(*id);
    sim::Equipment mine, sold;
    CHECK(o && sim::ai::opponent_equipment(sdb, program, *o, mine) && sim::stock_car(sdb, program, o->car, sold));
    CHECK(mine.part(0x08) == db::part_id(38, *id) && sold.part(0x08) == db::kNoPart);
    CHECK(mine.h(sim::Equipment::OutputFactor) == 1150 && mine.b(sim::Equipment::AsccIndices) == 1);
    sim::Spec a, b;
    sim::build_spec(sdb, program, mine, 0, a), sim::build_spec(sdb, program, sold, 0, b);
    const float pa = sim::spec_figures(a).ps, pb = sim::spec_figures(b).ps;
    CHECK(pa > pb * 1.1f && pa < pb * 1.2f);
    // The pool's paint: -1 leaves 0xFF (the race's choice); a grid entry drawn from a pool builds the same way.
    CHECK(mine.b(0x18) == 0xFF || mine.b(0x18) < sim::ai::paint_count(sdb, mine));
    sim::ai::GridEntry g;
    g.car = o->car, g.enemy = o->enemy, g.paint = 0;
    sim::Equipment again;
    CHECK(sim::ai::entry_equipment(sdb, program, ev, g, again) && again.h(sim::Equipment::OutputFactor) == 1150 && again.b(0x18) == 0);
}

TEST(ai_disc_rubber_band) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // A band that hurries the cars behind car 0 by up to 10 % from 20 m back and slows those ahead by up to 20 %: each
    // car's time scale follows its gap to the car between it and car 0 (the reference when every car is driven), the factors
    // chained outwards: with three cars within 0.8 x 0.8 .. 1.1 x 1.1.
    sim::ai::Pace pace;
    pace.band_mode = 0, pace.fast_pct = 10, pace.fast_start_m = 20, pace.fast_full_10m = 8, pace.slow_pct = 20, pace.slow_start_m = 20,
    pace.slow_full_10m = 8;
    float lo = 2, hi = 0;
    bool behind_hurried = false;
    const sim::ai::FieldRun run = sim::ai::run_field(*disc, "tsukuba", {"ka_01", "_1100_standard_type11_49", "s500_63"}, 1, pace, 0,
                                                     [&](const sim::Session& s, const sim::ai::Field& f) {
                                                         for (size_t i = 0; i < s.cars().size(); ++i) {
                                                             const float k = f.time_scale(i);
                                                             lo = std::min(lo, k), hi = std::max(hi, k);
                                                             if (k > 1.0f && s.record(i).time_scale.host() == k) behind_hurried = true;
                                                         }
                                                     });
    CHECK(run.cars.size() == 3);
    std::printf("  time scales %.3f .. %.3f\n", double(lo), double(hi));
    CHECK(lo >= 0.64f - 1e-4f && hi <= 1.21f + 1e-4f && hi > 1.0f);
    CHECK(behind_hurried); // the session steps the car by the band's scale
}

TEST(ai_disc_grid_paints) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb db = sim::ai::open_specdb(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const db::RaceEvents events(db, [disc](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = disc->volume().find(path);
        return e ? std::optional<Bytes>(disc->volume().read(*e)) : std::nullopt;
    });
    // Six of one car, every paint the race's to choose: each gets one of the car's paints, and with ten or more to choose
    // from no two share a colour.
    std::vector<sim::ai::GridEntry> grid(6);
    for (auto& g : grid) g.car = "ka_01";
    const std::vector<sim::Equipment> eq = sim::ai::grid_equipment(db, program, events, grid, 12345);
    CHECK(eq.size() == 6);
    const int n = sim::ai::paint_count(db, eq[0]);
    std::printf("  ka_01: %d paints, chosen", n);
    std::vector<int> seen;
    for (const auto& e : eq) {
        std::printf(" %d", e.b(0x18));
        CHECK(e.b(0x18) < n);
        seen.push_back(e.b(0x18));
    }
    std::printf("\n");
    std::sort(seen.begin(), seen.end());
    if (n >= 10) CHECK(std::adjacent_find(seen.begin(), seen.end()) == seen.end());
}

TEST(ai_disc_pit_and_finish) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb db = sim::ai::open_specdb(*disc);
    const sim::Program program = sim::Program::load(*disc);
    std::vector<sim::ai::FieldCar> cars(2);
    cars[0].name = "ka_01", cars[1].name = "s500_63";
    for (auto& c : cars) CHECK(sim::stock_car(db, program, c.name, c.entry.equipment));
    // Car 1 sent down the pit lane at 10 s drives its line 3 (kind 2) at the pit speed; car 0's run ended at 20 s with the
    // stop driver (mode 1) comes to rest (kind 8).
    int step = 0;
    bool on_pit_line = false, stopped = false;
    float pit_top = 0;
    sim::ai::run_field(*disc, "tsukuba", cars, 3, {}, 60, {}, [&](sim::Session& s, sim::ai::Field& f) {
        ++step;
        if (step == 600) CHECK(f.pit_enter(1));
        if (step == 1200) f.finish(0, 1);
        if (step > 600 && f.line(1) == 3 && f.kind(1) == 2) {
            on_pit_line = true;
            if (step > 1500) pit_top = std::max(pit_top, s.cars()[1].speed);
        }
        if (f.kind(0) == 8) stopped = true;
    });
    std::printf("  pit lane: %s, its speed after 15 s at most %.0f km/h; the stop driver: %s\n", on_pit_line ? "driven" : "not driven",
                double(pit_top) * 3.6, stopped ? "stopped" : "not stopped");
    CHECK(on_pit_line);
    CHECK(stopped);
}
