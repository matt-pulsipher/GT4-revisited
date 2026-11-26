// Gran Turismo 4 Prologue (docs/formats/prologue.md): its database read in Gran Turismo 4's layout,
// its roads as Runways, its drivers' lines from the course files, its program's tables, and a race
// run on them; on made-up data, then on the Prologue disc of GT4R_DISC.

#include "test_framework.h"

#include "formats/model/brake_model.h"
#include "formats/model/car_model.h"
#include "gt4r/formats/course/ai_line_course.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/db/specdb_prologue.h"
#include "gt4r/race/car_visual.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/sim/session.h"

#include "gt4r/race/gadgets.h"
#include "gt4r/race/sections.h"
#include "gt4r/race/strobe.h"
#include "gt4r/sim/ai/demo_driver.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/grid.h"

#include <cmath>
#include <cstring>
#include <set>

using namespace gt4r;

namespace {

Disc* prologue_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::Prologue) return d.get();
    return nullptr;
}

db::SpecDb open_db(Disc& disc, const char* language = "japanese") {
    gt3r::Volume* vol = &disc.volume();
    return db::SpecDb(
        [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        },
        [vol](const std::string& dir) { return vol->list_dir(dir); }, language);
}

void put32(Bytes& d, size_t o, u32 v) {
    for (size_t k = 0; k < 4; ++k) d[o + k] = u8(v >> (8 * k));
}

} // namespace

// The brake parts of its cars (Gran Turismo 3's race/brake.bin and the info blocks' records, docs/formats/prologue.md "Cars"):
// every car's records fall in the file's tables, and each car with a rotor has its parts and, where its rotor glows, the glow.
TEST(disc_prologue_brake_parts) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    gt3r::Volume& vol = disc->volume();
    const gt3r::Volume::Entry* be = vol.find("race/brake.bin");
    CHECK(be != nullptr);
    if (!be) return;
    const Bytes bb = vol.read(*be);
    CHECK(gt3r::BrakeParts::is_brake_bin(bb));
    const gt3r::BrakeParts bin = gt3r::BrakeParts::parse(bb);
    CHECK(bin.rotor_flags.size() == 7 && bin.caliper_place.size() == 30);
    db::SpecDb sdb = open_db(*disc);
    size_t cars = 0, with_parts = 0, calipers = 0, glows = 0;
    std::set<std::string> seen;
    for (const db::CarEntry& c : sdb.cars()) {
        if (!seen.insert(c.model).second) continue;
        const gt3r::Volume::Entry* e = vol.find("car/lod/" + c.model);
        if (!e) continue;
        const Bytes file = vol.read(*e);
        if (!race::CarVisual::is_gt3_car(file) || !gt3r::CarFile::is_car(file)) continue;
        ++cars;
        const std::array<gt3r::BrakeRecord, 2> rec = gt3r::parse_brake_records(gt3r::CarFile::parse(file).info);
        bool parts = true;
        for (int a = 0; a < 2; ++a) {
            CHECK(rec[a].rotor < 0 || bin.has_rotor(rec[a].rotor));
            CHECK(rec[a].caliper < 0 || bin.has_caliper(rec[a].caliper));
            parts &= !gt3r::build_brake_parts(bin, rec[a], a == 1, 0).meshes.empty();
            calipers += rec[a].caliper >= 0;
            glows += !gt3r::build_brake_glow(bin, rec[a], false).meshes.empty();
        }
        with_parts += parts;
    }
    std::printf("  %zu cars: %zu with both axles' parts, %zu axles with a caliper, %zu with a glowing rotor\n", cars, with_parts, calipers, glows);
    CHECK(cars > 70 && with_parts == cars);
}

// A table stored plain, as Prologue's are: the rows at their offsets, every one read.
TEST(prologue_plain_table) {
    Bytes f(0x10 + 2 * 8 + 2 * 16);
    std::memcpy(f.data(), "GTDB", 4);
    put32(f, 4, 0x80000), put32(f, 8, 2), put32(f, 12, 13);
    put32(f, 0x10, 5), put32(f, 0x14, 0), put32(f, 0x18, 9), put32(f, 0x1C, 16);
    for (size_t i = 0; i < 13; ++i) f[0x20 + i] = u8(i), f[0x30 + i] = u8(0x80 + i);
    CHECK(db::prologue::is_plain_table(f));
    const db::Table t = db::Table::parse(f); // (through the hook in Table::parse)
    CHECK(t.size() == 2 && t.row_size() == 13 && t.row(5).size() == 13 && t.row(9)[12] == 0x8C && t.row(7).empty());
    // Gran Turismo 4's packed tables have flags bit 0.
    put32(f, 4, 0x80001);
    CHECK(!db::prologue::is_plain_table(f));
    // The table ids after the four tables Prologue lacks.
    CHECK(db::prologue::table_id(26) == 26 && db::prologue::table_id(27) == 31 && db::prologue::table_id(38) == 42);
    CHECK(db::prologue::table_id(0xFFFFFFFFu) == 0xFFFFFFFFu);
    CHECK(db::prologue::file_name("VARIATIONjapanese") == "VARIATION" && db::prologue::file_name("CHASSIS") == "CHASSIS");
}

// The database: every table, the cars with their models on the disc and their names, and the parts'
// rows in Gran Turismo 4's columns, so a car's specification builds.
TEST(disc_prologue_database) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    db::SpecDb sdb = open_db(*disc);
    CHECK(sdb.prologue() && sdb.folder() == "specdb/GT4_PROLOGUE1000/");
    size_t rows = 0;
    for (const std::string& n : sdb.table_names()) {
        const db::Table* t = sdb.table(n);
        CHECK(t != nullptr);
        if (t) rows += t->size();
    }
    std::printf("  %zu tables, %zu rows\n", sdb.table_names().size(), rows);
    CHECK(sdb.table_names().size() == 42 && rows > 3000);
    // The labels' table ids are Gran Turismo 4's.
    CHECK(sdb.labels("TIRESIZE") && sdb.labels("TIRESIZE")->table_id == 31);
    CHECK(sdb.labels("DEFAULT_PARTS") && sdb.labels("DEFAULT_PARTS")->table_id == 36);
    const std::vector<db::CarEntry> cars = sdb.cars();
    CHECK(cars.size() == 83);
    size_t named = 0, modelled = 0, specs = 0;
    const sim::Program program = sim::Program::load(*disc);
    for (const db::CarEntry& c : cars) {
        named += !c.name.empty();
        const gt3r::Volume::Entry* e = disc->volume().find("car/lod/" + c.model);
        if (e && c.paints > 0) {
            const Bytes f = disc->volume().read(*e);
            const bool model = race::CarVisual::is_gt3_car(f) || gt3r::has_magic(f, "CAR4");
            modelled += model;
            if (!model) std::printf("  %s (%s): not a car file\n", c.label.c_str(), c.model.c_str());
        } else {
            std::printf("  %s (%s): no model file, %u paints\n", c.label.c_str(), c.model.c_str(), c.paints);
        }
        sim::Equipment eq;
        if (sim::stock_car(sdb, program, c.label, eq)) {
            sim::Spec s;
            sim::build_spec(sdb, program, eq, 0, s);
            specs += s.h(0x20) > 500 && s.gears() >= 1;
        }
    }
    std::printf("  %zu cars: %zu named, %zu with their model, %zu specs\n", cars.size(), named, modelled, specs);
    CHECK(named == cars.size() && modelled == cars.size() && specs == cars.size());
    // The Subaru Impreza rally car '03: 1,230 kg, six gears, four wheel drive, 300 PS (the catalog's).
    sim::Equipment eq;
    CHECK(sim::stock_car(sdb, program, "impreza_wrc_03", eq));
    sim::Spec s;
    sim::build_spec(sdb, program, eq, 0, s);
    CHECK(s.h(0x20) == 1230 && s.gears() == 6 && s.drive() == 2);
    const ByteSpan engine = db::part_row(sdb, eq.part(sim::Equipment::Engine));
    CHECK(engine.size() == 113 && rd16(engine, 0x1A) == 300 && engine[0x6F] == 100);
    // Every engine's sound file is on the disc (ENGINE +0x18).
    const db::Table* engines = sdb.table("ENGINE");
    size_t sounds = 0;
    for (size_t i = 0; engines && i < engines->size(); ++i) {
        char p[32];
        std::snprintf(p, sizeof p, "sound/engine/%05u", rd16(engines->row_at(i), 0x18));
        sounds += disc->volume().find(p) != nullptr;
    }
    CHECK(engines && sounds == engines->size());
    // The width across the tires, made from the tread and the tire's width.
    const ByteSpan chassis = db::part_row(sdb, eq.part(sim::Equipment::Chassis));
    CHECK(chassis.size() == 37 && rd16(chassis, 0x12) > 1500 && rd16(chassis, 0x12) < 2000);
    // The courses by their labels.
    CHECK(sdb.courses().size() == 64);
}

// The arcade's tire grip: the street and the sports tires' compounds on the car's own sizes, the street tires with the least grip.
TEST(disc_prologue_arcade_tires) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    db::SpecDb sdb = open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    sim::Spec own, low, high;
    sim::Equipment eq;
    CHECK(sim::stock_equipment(sdb, "demio_sport_03", eq));
    sim::build_spec(sdb, program, eq, 0, own);
    CHECK(!sim::prologue::fit_grip_tires(sdb, program, "medium", eq) && !sim::prologue::fit_grip_tires(sdb, program, "unselectable", eq));
    sim::Equipment e = eq;
    CHECK(sim::prologue::fit_grip_tires(sdb, program, "low", e));
    sim::build_spec(sdb, program, e, 0, low);
    e = eq;
    CHECK(sim::prologue::fit_grip_tires(sdb, program, "high", e));
    sim::build_spec(sdb, program, e, 0, high);
    const auto grip = [](const sim::Spec& s, int axle) { return int(s.b(sim::Spec::tire_block(3 * axle))); };
    std::printf("  front / rear grips: street %d / %d, its own %d / %d, sports %d / %d\n", grip(low, 0), grip(low, 1), grip(own, 0), grip(own, 1), grip(high, 0), grip(high, 1));
    for (int axle = 0; axle < 2; ++axle) {
        CHECK(grip(low, axle) < grip(high, axle) && grip(low, axle) < grip(own, axle));
        for (const u32 at : {0x1C8u, 0x1CAu, 0x1CCu}) CHECK(low.b(at + u32(axle)) == own.b(at + u32(axle))); // (the sizes)
    }
}

// Every course's road (in either table) reads as a Runway: the start grid on the road and
// facing along the course, the checkpoints rising, every triangle found through the lookup tree.
TEST(disc_prologue_roads) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    size_t roads = 0, grids = 0, grid_on_road = 0, along = 0, tris = 0, found = 0;
    for (const std::string& name : disc->volume().list_dir("crs")) {
        if (name.empty() || name.back() == '/') continue;
        const Bytes d = disc->volume().read(*disc->volume().find("crs/" + name));
        if (!course::CourseFile::looks_like_course(d)) continue;
        const course::CourseFile f = course::CourseFile::parse(d);
        const std::optional<course::Runway> r = course::course_road(f, d);
        CHECK(r.has_value());
        if (!r) continue;
        ++roads;
        CHECK(r->length > 50 && !r->clusters.empty() && (r->checkpoints.size() >= 10 || name.rfind("ss_", 0) == 0)); // (the photo stages ss_*: 3 .. 17)
        for (const course::Runway::Place& g : r->grid) {
            ++grids;
            float y;
            const bool on = r->height_at(g.pos.x, g.pos.z, 1e30f, y); // (some places' heights are only rough: New York's are at 0)
            grid_on_road += on;
            if (!on) std::printf("  %s: grid place %zu at (%.1f, %.1f, %.1f) is not on the road\n", name.c_str(), size_t(&g - r->grid.data()), double(g.pos.x), double(g.pos.y), double(g.pos.z));
        }
        // The first place's heading leads to rising distances.
        if (!r->grid.empty()) {
            const sim::RoadGround ground(*r);
            const course::Runway::Place& g = r->grid[0];
            float v0 = 0, v1 = 0;
            const bool a = ground.distance_along({g.pos.x, g.pos.y, g.pos.z}, v0);
            const bool b = ground.distance_along({g.pos.x + 10 * std::sin(g.heading), g.pos.y, g.pos.z + 10 * std::cos(g.heading)}, v1);
            float dv = v1 - v0;
            if (dv < -r->length / 2) dv += r->length;
            const bool ok = a && b && dv > 5 && dv < 15;
            along += ok;
            if (!ok) std::printf("  %s: the first place at %.1f m, 10 m on at %.1f m\n", name.c_str(), double(v0), double(v1));
        }
        for (size_t t = 0; t < r->triangles.size(); t += 7) {
            const course::Runway::Triangle& tr = r->triangles[t];
            const course::Vec3 &p = r->vertices[tr.v[0]].pos, &q = r->vertices[tr.v[1]].pos, &s = r->vertices[tr.v[2]].pos;
            const float x = (p.x + q.x + s.x) / 3, z = (p.z + q.z + s.z) / 3;
            const int c = r->cluster_at(x, z);
            ++tris;
            if (c >= 0)
                for (u16 k : r->clusters[size_t(c)].triangles) found += k == t;
        }
    }
    std::printf("  %zu roads: %zu of %zu grid places on the road, %zu first places facing along, %zu of %zu triangles in their cell\n",
                roads, grid_on_road, grids, along, found, tris);
    // (The three gymkhanas' and Laguna Seca's first places face against the checkpoints' order,
    // Las Vegas's lies before the first checkpoint, and two photo stages have three or eight
    // checkpoints: the seven that do not.)
    CHECK(roads == 62 && found == tris && along + 7 >= roads);
    CHECK(grid_on_road == grids);
}

// The drivers' lines and sets of the course files, and a field of computer drivers lapping
// Tsukuba on Prologue's cars, its program's tables in the race core.
TEST(disc_prologue_race) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    const Bytes d = disc->volume().read(*disc->volume().find("crs/tsukuba"));
    const course::CourseFile f = course::CourseFile::parse(d);
    const std::optional<course::AiData> ai = course::ai_from_course(f, d);
    CHECK(ai && ai->lines[6].size() > 10 && ai->sets[0] && ai->sets[3]);
    // The program: recognized, its tables placed.
    const sim::Program program = sim::Program::load(*disc);
    const sim::PrologueProgram pp = sim::PrologueProgram::locate(program);
    CHECK(!program.known() && pp.recognized());
    gt3r::sim::exact::ProgramData pd = gt3r::sim::exact::ProgramData::synthetic();
    CHECK(pp.apply(program, pd));
    CHECK(pd.b(gt3r::sim::exact::ProgramData::Table::Static, 0) == 0x0C); // (the air density byte, x 10)
    for (const std::unique_ptr<Disc>& other : test::discs())
        if (other->info().game == Game::GT4) CHECK(!sim::PrologueProgram::locate(sim::Program::load(*other)).recognized());
    // Two cars, a lap each.
    const sim::ai::FieldRun run = sim::ai::run_field(*disc, "tsukuba", std::vector<std::string>{"impreza_wrc_03", "civic_r_98"}, 1);
    for (const sim::ai::CarRun& c : run.cars) {
        std::printf("  %s: %s lap %.3f s, top %.1f km/h\n", c.car.c_str(), c.finished ? "finished" : "did not finish",
                    c.laps.empty() ? 0.0 : c.laps[0], double(c.top_speed) * 3.6);
        CHECK(c.finished && !c.laps.empty() && c.laps[0] > 55 && c.laps[0] < 100);
    }
}

// Prologue's own race step (tools/gt4diff's Prologue harness, rules_prologue.h): its program's step run in the interpreter on
// a test drive, each of our routines run in place at every call of the program's and every field of the record compared:
// the engine's routines Prologue shares and Prologue's own, equal at every call.
#include "prologue_taps.h"

TEST(disc_prologue_step_routines_equal) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    for (const char* which : {"engine", "prologue", "input"}) {
        std::string err;
        const std::vector<diff::prologue::PTapStat> stats = diff::prologue::check(*disc, "tsukuba", "civic_r_98", 1200, which, err);
        if (!err.empty()) std::printf("  %s\n", err.c_str());
        CHECK(err.empty());
        // (the engine's routines Prologue does not share are tapped too, to show how they differ)
        static const std::set<std::string> differ = {"slip ratios", "tire slip force", "forward step", "lateral and yaw", "wheel steer", "aids",
                                                     "attitude", "ground frame", "rolling resistance", "brake heat",
                                                     "terrain factor", "pad rumble", "steer force", "tire effects", "engine sound",
                                                     "view follow", "speed bookkeeping", "engine's driver input"};
        for (const diff::prologue::PTapStat& st : stats) {
            std::printf("  %s: %s %llu of %llu calls equal\n", which, st.name.c_str(), static_cast<unsigned long long>(st.clean),
                        static_cast<unsigned long long>(st.calls));
            CHECK(st.calls > 0 || st.name == "engine's driver input");
            if (!differ.count(st.name)) CHECK(st.clean == st.calls);
        }
    }
    // Steering wheels (the controller kinds 4 and 6) and a digital pad: the input and the steering wheel's force.
    for (u8 kind : {u8(6), u8(4), u8(0)})
        for (const char* which : {"prologue", "input"}) {
            std::string err;
            const std::vector<diff::prologue::PTapStat> stats = diff::prologue::check(*disc, "tsukuba", "civic_r_98", 700, which, err, kind);
            CHECK(err.empty());
            for (const diff::prologue::PTapStat& st : stats) {
                if (st.name != "Prologue's steering force" && st.name != "Prologue's driver input") continue;
                std::printf("  controller %d: %s %llu of %llu calls equal\n", kind, st.name.c_str(), static_cast<unsigned long long>(st.clean),
                            static_cast<unsigned long long>(st.calls));
                for (const auto& [name, f] : st.fields) std::printf("      %s: %s\n", name.c_str(), f.example.c_str());
                CHECK(st.calls > 0 && st.clean == st.calls);
            }
        }
}

// The license tests' gold, silver and bronze times from their RACE rows (Prologue's +0x5C .. +0x61 minutes and seconds,
// +0x0C .. +0x11 milliseconds, as the program's 0x409B70 / 0x409BB0 / 0x409BF0 read them), in Gran Turismo 4's columns.
#include "gt4r/formats/db/race_events.h"

TEST(disc_prologue_license_target_times) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    db::SpecDb db = open_db(*disc);
    gt3r::Volume* vol = &disc->volume();
    db::RaceEvents events(db, [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    });
    int with_gold = 0;
    for (int n = 1; n <= 46; ++n) {
        char label[16];
        std::snprintf(label, sizeof label, "l0p%04d", n);
        const std::optional<db::RaceEvent> e = events.event(label);
        CHECK(e.has_value());
        if (!e) continue;
        if (e->target_ms[0] > 0) ++with_gold;
        // (a test's times rise from gold to bronze where it has them)
        if (e->target_ms[1] > 0) CHECK(e->target_ms[0] <= e->target_ms[1]);
        if (e->target_ms[2] > 0) CHECK(e->target_ms[1] <= e->target_ms[2]);
        if (n <= 6) std::printf("  %s on %s, %d laps: %u / %u / %u ms\n", label, e->course.c_str(), e->laps, e->target_ms[0], e->target_ms[1], e->target_ms[2]);
        // (the laps, RACE +0x29: lesson 1 two, as the original runs it; lesson 2 one, its road's section)
        if (n == 1) CHECK(e->laps == 2);
        if (n == 2) CHECK(e->laps == 1);
    }
    std::printf("  %d of 46 tests have a gold time\n", with_gold);
    CHECK(with_gold == 46);
}

// The license tests' rules from their RACE rows (the race's copy 0x3C5978 and the switch 0x409AB8), in Gran Turismo 4's
// columns: every test a wall-hit limit, tests with cones a count, tests with a time limit one.
#include "gt4r/race/license_rules.h"

TEST(disc_prologue_license_rules) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    db::SpecDb db = open_db(*disc);
    gt3r::Volume* vol = &disc->volume();
    db::RaceEvents events(db, [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    });
    int tests = 0, walls = 0, cones = 0, timed = 0;
    for (int n = 1; n <= 46; ++n) {
        char label[16];
        std::snprintf(label, sizeof label, "l0p%04d", n);
        const std::optional<db::RaceEvent> e = events.event(label);
        if (!e) continue;
        const race::LicenseRules r = race::LicenseRules::from_row(e->bytes);
        ++tests;
        walls += r.wall_limit > 0, cones += r.pylons > 0, timed += r.time_limit > 0;
        std::printf("  %s: kind %d, walls %d, cars %d, cones %d, time %d, +41 %d, +4A %d\n", label, r.kind, r.wall_limit, r.car_limit, r.pylons,
                    r.time_limit, r.x41, r.x4a);
    }
    CHECK(tests == 46);
    CHECK(walls > 0 && cones > 0 && timed > 0);
}

// The roads keep Gran Turismo 3's road block for the program's own ground query (Runway::gtrw), and a license course's
// goal distance (the block's header +0x1C) lies inside the lap where its test ends: L_fisco6, test 2, 203 m from the line.
TEST(disc_prologue_license_goal) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 Prologue disc");
    const gt3r::Volume::Entry* e = disc->volume().find("crs/L_fisco6");
    CHECK(e != nullptr);
    if (!e) return;
    const Bytes d = disc->volume().read(*e);
    const course::CourseFile f = course::CourseFile::parse(d);
    const std::optional<course::Runway> r = course::course_road(f, d);
    CHECK(r && r->gtrw);
    if (!r) return;
    std::printf("  L_fisco6: lap %.1f m, goal %.1f m\n", double(r->length), double(r->goal_v));
    CHECK(r->goal_v > 200 && r->goal_v < 206 && r->goal_v < r->length - 1);
}

// Prologue's spec builder (0x3B3998) and converter (0x3C74B8) as the program runs them in the interpreter on every car of
// its database, against ours (convert_prologue.h): every spec byte and every word of the parameter block equal.
#include "prologue_race.h"

TEST(disc_prologue_converter_equal) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    const diff::prologue::ConvertStats st = diff::prologue::convert_compare(*disc, "tsukuba", "civic_r_98", false);
    std::printf("  %d cars: %d blocks, %d specs equal; the race's start's values %s\n", st.cars, st.equal_blocks, st.equal_specs,
                st.race_start_equal ? "equal" : "differ");
    CHECK(st.err.empty());
    CHECK(st.cars >= 80);
    CHECK(st.equal_blocks == st.cars);
    CHECK(st.equal_specs == st.cars);
    CHECK(st.race_start_equal);
}

// The pass codes (menu/prologue_passcode.h): the program's 0x1A31A0 in the interpreter against ours on 64 sets of fields.
TEST(disc_prologue_pass_codes_equal) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    CHECK(diff::prologue::run_passcode(*disc, 64));
}

// Phase 14 (0x3DC1C8) in a race of three cars, the program's own computer drivers driving two: the cars' slipstream (and
// every other field but the drivers' zone cells and contact) equal to the engine's pairs pass, which the session runs.
TEST(disc_prologue_pairs_pass_equal) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    const diff::prologue::NearStats s = diff::prologue::near_check(*disc, "L_fisco6", "impreza_wrc_03,impreza_wrc_03,impreza_wrc_03", 2000, false);
    std::printf("  %llu car records, %llu equal, %llu with a slipstream\n", (unsigned long long)s.records, (unsigned long long)s.clean,
                (unsigned long long)s.with_slip);
    CHECK(s.err.empty());
    CHECK(s.records == 6000 && s.clean == s.records);
    CHECK(s.with_slip > 0);
}

// The race's rules on our session (race_rules.h): lesson 1's speed cap (0x3E1C20: 50 km/h, as the original's lesson 1 drives),
// and the Lupo cup's penalty: a hit at the wall past its limit, then the cap for ten seconds. Full throttle, no steering.
#include "gt4r/sim/race_rules.h"

TEST(disc_prologue_test_starts) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    db::SpecDb db = open_db(*disc);
    gt3r::Volume* vol = &disc->volume();
    db::RaceEvents events(db, [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    });
    const sim::Program program = sim::Program::load(*disc);
    int moving = 0, others = 0;
    for (int n = 1; n <= 48; ++n) {
        char label[16];
        std::snprintf(label, sizeof label, "l0p%04d", n);
        const std::optional<db::RaceEvent> e = events.event(label);
        if (!e) continue;
        const sim::RaceRules r = sim::race_rules(e->bytes, program);
        if (r.start_speed[0] == 0) continue;
        std::printf("  %s on %s:", label, e->course.c_str());
        for (size_t k = 0; k < 6; ++k) std::printf(" %d km/h %d m%s", r.start_speed[k], r.start_distance[k], k < 5 ? "," : "\n");
        ++moving;
        CHECK(r.start_distance[0] < 0 && r.start_distance[0] > -400);
        for (size_t k = 1; k < 6; ++k) {
            CHECK(r.start_speed[k] != 0); // (its own or car 0's)
            if (r.start_distance[k] == 0) continue;
            ++others;
            CHECK(r.start_distance[k] != r.start_distance[0]);
            CHECK(std::abs(r.start_distance[k]) < 2000);
        }
    }
    std::printf("  %d tests on the move, %d other cars' places\n", moving, others);
    CHECK(moving > 20);
    CHECK(others > 9);
}

TEST(disc_prologue_race_rules) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    db::SpecDb db = open_db(*disc);
    gt3r::Volume* vol = &disc->volume();
    db::RaceEvents events(db, [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    });
    const sim::Program program = sim::Program::load(*disc);
    auto run = [&](const char* label, int seconds) {
        const std::optional<db::RaceEvent> e = events.event(label);
        CHECK(e.has_value());
        const sim::RaceRules r = sim::race_rules(e->bytes, program);
        std::printf("  %s: cap %d, penalty %d s at %d km/h, walls %d, cars %d\n", label, r.speed_cap[0], r.hits.penalty, r.penalty_cap, r.hits.wall_limit, r.hits.car_limit);
        const std::vector<std::string> labels{"civic_r_98"};
        auto session = sim::Session::open(*disc, "tsukuba", labels);
        session->set_race_rules(r.hits, r.speed_cap, r.penalty_cap);
        std::vector<race::CarInput> in(1);
        for (int k = 0; k < sim::Session::kHoldSteps; ++k) session->hold_once(in);
        in[0].throttle = 1;
        struct {
            float top = 0, penalty_top = 0;
            int penalty = 0, steps = 0;
        } out;
        for (int k = 0; k < seconds * 60; ++k) {
            session->step_once(in);
            const float v = std::fabs(session->cars()[0].speed) * 3.6f;
            out.top = std::max(out.top, v);
            const int p = session->penalty_seconds(0);
            out.penalty = std::max(out.penalty, p);
            if (p > 0) ++out.steps, in[0].brake = 0;
            if (p > 0 && p < 5) out.penalty_top = std::max(out.penalty_top, v);
        }
        return out;
    };
    const auto lesson = run("l0p0001", 12);
    std::printf("    top %.2f km/h\n", double(lesson.top));
    CHECK(lesson.top > 49.0f && lesson.top < 51.0f);
    CHECK(lesson.penalty == 0);
    const auto cup = run("gt4p_lupo_cup_p", 40);
    std::printf("    top %.2f km/h, penalty %d s (%d steps), top in its last 5 s %.2f km/h\n", double(cup.top), cup.penalty, cup.steps, double(cup.penalty_top));
    CHECK(cup.top > 80.0f);
    CHECK(cup.penalty == 10);
    CHECK(cup.steps >= 600);
    CHECK(cup.penalty_top < 52.0f);
}

// Prologue's opening shots (race/start_camera_prologue.h): the paths of slots 45 and 46 on every course that has them, cut in
// the replay's last seconds (the original's grid page: one shot a second from 4 s before its end), each shot near the car.
#include "gt4r/race/start_camera_prologue.h"

TEST(disc_prologue_start_camera) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    const gt3r::Volume::Entry* e = disc->volume().find("crs/tsukuba");
    CHECK(e != nullptr);
    if (!e) return;
    const Bytes d = disc->volume().read(*e);
    const course::CourseFile f = course::CourseFile::parse(d);
    for (int k = 0; k < 2; ++k) {
        const ByteSpan s = f.section(d, size_t(k ? race::kSlotPrologueReplayStart : race::kSlotPrologueStart));
        CHECK(race::ReplayScript::is_script(s));
        if (!race::ReplayScript::is_script(s)) return;
        const race::ReplayScript script = race::ReplayScript::parse(s);
        race::PrologueStartCamera cam;
        cam.begin(&script, k == 1, 600);
        gt3r::render::Mat4 body; // (the car at the origin)
        int steps = 0, cuts = 0, dissolves = 0;
        std::vector<int> at;
        gt3r::render::Vec3 last{};
        for (; cam.active() && steps < 2000; cam.step(), ++steps) {
            race::ViewPose p;
            CHECK(cam.pose(body, p));
            CHECK(std::fabs(p.eye.x) < 60 && std::fabs(p.eye.y) < 20 && std::fabs(p.eye.z) < 60);
            const gt3r::render::Vec3 to = p.eye - last;
            if (steps && gt3r::render::length(to) > 1.0f) ++cuts, at.push_back(steps);
            if (cam.dissolve() > 0) ++dissolves;
            last = p.eye;
        }
        std::printf("  %s: %d steps, %d cuts%s%s, %d dissolves\n", k ? "replay" : "race", steps, cuts, at.empty() ? "" : ", the first at step ",
                    at.empty() ? "" : std::to_string(at[0]).c_str(), dissolves);
        CHECK(steps == (k ? 661 : 541));
        CHECK(cuts >= (k ? 3 : 2));
        CHECK(dissolves == (k ? 0 : 3));
    }
}

// Every step of 1,200 from the program's state, ours against the program's (two touches of a wall in them): every field of the
// record equal but the flaps (the shared random stream), the steering force and rumble (not run), the distance along the
// course, the lap's and the drivers' words, and the view's pitch lag.
TEST(disc_prologue_whole_step_equal) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    std::vector<std::string> fields;
    CHECK(diff::prologue::whole_step_fields(*disc, "tsukuba", "civic_r_98", 1200, fields));
    static const std::set<std::string> kKnown = {"steer_force", "ffb", "lag_pitch_diff", "lag_heading_diff", "f3e4", "prev.f3e4", "laps", "x535", "x51c", "feedback", "hit_part"};
    for (const std::string& f : fields) {
        const bool known = kKnown.count(f) || f.rfind("ai.", 0) == 0;
        if (!known) std::printf("  %s differs\n", f.c_str());
        CHECK(known);
    }
}

// What the race's start makes of Prologue's own tables for its step, against the program's conductor on a single race's and a
// lesson's row (gt4r_diff prologue-start): the engine sound's scales, the tire wear's rules (with made-up ones too: no row has
// any), the class grips; a single race has its wear rules on, a lesson not. The automatic gearbox's settings are the
// controller settings' +0x08 .. +0x0B (0x3CD6B8 reads them there).
TEST(disc_prologue_race_start_tables) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    const auto st = diff::prologue::start_tables_check(*disc, true);
    std::printf("  %d of %d values equal %s\n", st.equal, st.checks, st.err.c_str());
    CHECK(st.err.empty());
    CHECK(st.checks == 72 && st.equal == st.checks);
    CHECK(st.single_wear && !st.license_wear);
    const sim::prologue::Tables t = sim::prologue::tables(sim::Program::load(*disc));
    CHECK(t.gears.mode == 1 && t.gears.down_throttle == 5 && t.gears.down_lift == 0 && t.gears.down_brake == 0);
}

// A license test's car is the first of its event's pool with that entry's parts and settings (the run's own: what the
// demonstrations' files keep of their car 0).
TEST(disc_license_car_is_the_pools) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("no Gran Turismo 4 disc");
    const sim::Program program = sim::Program::load(*disc);
    gt3r::Volume* vol = &disc->volume();
    db::SpecDb sdb = open_db(*disc, "american");
    db::RaceEvents events(sdb, [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    });
    int runs = 0, equal = 0;
    for (const std::string& label : events.labels()) {
        if (label.rfind("usl0", 0) != 0) continue;
        const auto run = sim::ai::DemoRun::load(*disc, program, label);
        const auto e = events.event(label);
        if (!run || !e) continue;
        const std::vector<db::Opponent> pool = events.pool(*e);
        const auto entries = run->entries();
        if (pool.empty() || entries.empty()) continue;
        sim::ai::GridEntry g;
        g.car = pool[0].car, g.paint = pool[0].paint, g.parts = pool[0].parts, g.param = pool[0].param, g.enemy = pool[0].enemy;
        sim::Equipment eq;
        CHECK(sim::ai::entry_equipment(sdb, program, events, g, eq));
        ++runs;
        std::string d;
        // (the record's padding +0x19 / +0x1A and its scratch words from +0x11C left out)
        for (size_t k = 0; k < eq.bytes.size(); ++k)
            if (k != 0x19 && k != 0x1A && k < 0x11C && eq.bytes[k] != entries[0].equipment.bytes[k]) {
                char t[40];
                std::snprintf(t, sizeof t, " +%zX %02X/%02X", k, eq.bytes[k], entries[0].equipment.bytes[k]);
                d += t;
            }
        if (d.empty()) ++equal;
        else std::printf("  %s:%s\n", label.c_str(), d.c_str());
    }
    // (The rest: the tests with the pace car, whose file lists it first, and B-8, whose car's rows are another region's.)
    std::printf("  %d demonstrations, %d equal in every part and setting\n", runs, equal);
    CHECK(runs > 50);
    CHECK(equal >= 44);
}

// A passed lesson's name entry in the results movie: one letter at a time when no name is kept, Down the next letter, accept
// the next box, cancel one back, the third letter the name; the cones of a lesson and its road's split points.
TEST(disc_prologue_name_entry_and_cones) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    gt3r::Volume* vol = &disc->volume();
    const gt3r::Volume::Entry* e = vol->find("menu/result_license.strb");
    CHECK(e != nullptr);
    if (!e) return;
    auto movie = std::make_shared<const race::StrobeMovie>(race::StrobeMovie::parse(vol->read(*e)));
    using V = race::StrobePlayer::Value;
    std::string entered;
    race::StrobePlayer player(movie, [&](const std::string& name, const std::vector<V>& args) {
        if (name == "GetRank") return V::number(0);
        if (name == "PrizeLevel") return V::number(2);
        if (name == "GetTime") return V::string("1'49.301");
        if (name == "GetDefaultName") return V::string("");
        if (name == "NameEntry" && !args.empty()) entered = args[0].to_string();
        return V{};
    });
    player.restart();
    auto run = [&](float seconds) {
        for (float t = 0; t < seconds; t += 1.0f / 60) player.advance(1.0f / 60);
    };
    run(12);
    auto letters = [&] {
        std::string out;
        for (const race::StrobePlayer::Draw& d : player.draw_list())
            if (d.text && d.words.size() == 1) out += d.words;
        return out;
    };
    CHECK(letters() == "A");
    for (int k = 0; k < 2; ++k) player.key(0x28), run(0.3f);
    player.key(0x26), run(0.3f);
    CHECK(letters() == "B");
    player.key(0x20), run(0.5f);
    CHECK(letters() == "BA");
    player.key(0x1B), run(0.5f);
    player.key(0x20), run(0.5f), player.key(0x20), run(0.5f);
    CHECK(letters() == "BAA");
    CHECK(entered.empty());
    player.key(0x20), run(0.5f);
    CHECK(entered == "BAA");

    const gt3r::Volume::Entry* c = vol->find("crs/L_fisco6");
    CHECK(c != nullptr);
    if (!c) return;
    const Bytes data = vol->read(*c);
    const course::CourseFile file = course::CourseFile::parse(data);
    const ByteSpan road = file.section(data, course::kSlotRoadGtrw);
    const race::Gadgets g = race::Gadgets::parse(file.section(data, course::kSlotGadgets), road);
    CHECK(g.placements.size() == 64);
    CHECK(g.kinds.size() == 3);
    for (const race::Gadgets::Placement& p : g.placements) CHECK(p.kind >= 0 && size_t(p.kind) < g.kinds.size());
    const std::vector<float> splits = race::road_list(road, 0);
    CHECK(splits.size() == 2);
    CHECK(splits.size() == 2 && splits[0] < splits[1] && splits[1] < 4459);
}

// Prologue's recorded runs (the 46 lessons' demonstrations and ten races'): every file reads.
#include "gt4r/formats/replay/demonstration_prologue.h"

TEST(disc_prologue_recorded_runs) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    const sim::Program program = sim::Program::load(*disc);
    const std::array<s32, 8> bases = replay::prologue::delta_bases(program);
    gt3r::Volume* vol = &disc->volume();
    int read = 0;
    for (std::string dir : vol->list_dir("replay/JP")) {
        if (!dir.empty() && dir.back() == '/') dir.pop_back();
        const gt3r::Volume::Entry* e = vol->find(replay::prologue::run_path(dir));
        if (!e) continue;
        std::string error;
        const auto run = replay::prologue::Run::parse(vol->read(*e), bases, &error);
        if (!run) {
            std::printf("  %s: %s\n", dir.c_str(), error.c_str());
            CHECK(false);
            continue;
        }
        ++read;
        const auto& f = run->streams[0].frames;
        std::printf("  %-18s %-14s event %llX course %llX time %d %d  entries %zu  streams %zu T %u kinds", dir.c_str(), run->race_class.c_str(), (unsigned long long)run->event,
                    (unsigned long long)run->course, run->time_ms, run->time2_ms, run->entries.size(), run->streams.size(), run->streams[0].frame_count);
        std::set<int> kinds;
        for (const auto& p : f) kinds.insert(p.kind());
        for (int k : kinds) std::printf(" %d", k);
        for (const auto& n : run->entries) std::printf("  [car %llX place %d style %d driver %d aids %d %d %d]", (unsigned long long)n.code(0), n.grid_place, n.pedal_style, n.driver_kind, n.x16a, n.asm_on, n.tcs_on);
        std::printf("\n");
    }
    CHECK(read >= 56);
}

// The lessons' demonstrations (recorded on the console) played through our race.
#include "gt4r/race/demonstration_run_prologue.h"

TEST(disc_prologue_demonstrations) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    db::SpecDb sdb = open_db(*disc);
    int exact = 0, ended = 0;
    const char* only = std::getenv("GT4R_LESSON");
    for (int k = 1; k <= 46; ++k) {
        char label[16];
        std::snprintf(label, sizeof label, "l0p%04d", k);
        if (only && std::string(only) != label) continue;
        race::prologue::Options o;
        if (const char* hold = std::getenv("GT4R_LESSON_HOLD")) o.hold = std::atoi(hold);
        if (const char* shift = std::getenv("GT4R_LESSON_SHIFT")) o.shift = std::atoi(shift);
        o.cones = !std::getenv("GT4R_LESSON_NOCONES");
        o.judge = !std::getenv("GT4R_LESSON_NOJUDGE");
        if (const char* every = std::getenv("GT4R_LESSON_LOG"))
            o.on_step = [n = std::atoi(every)](size_t step, const sim::Session& s) {
                const race::CarState& c = s.cars()[0];
                if (step % size_t(n) == 0)
                    std::printf("    step %zu: %.2f km/h at (%.2f, %.2f, %.2f) v %.1f lap %d gear %d thr %.2f brk %.2f steer %.3f contact %d\n", step, double(c.speed) * 3.6, double(c.position.x),
                                double(c.position.y), double(c.position.z), double(c.course_v), c.lap, c.gear, double(c.throttle), double(c.brake), double(c.steer), c.contact_kind);
            };
        const race::DemonstrationRun r = race::prologue::run_demonstration(*disc, sdb, label, o);
        if (only || std::getenv("GT4R_LESSON_ALL"))
            std::printf("  %s %-12s %-22s recorded %6d ended %6d fail %d steps %zu kind %d %s\n", label, r.course.c_str(), r.car.c_str(), r.recorded_ms, r.ended_ms, r.fail, r.steps, r.kind, r.error.c_str());
        if (only && !r.equipment.empty()) std::printf("      %s\n", r.equipment.c_str());
        exact += r.exact(), ended += r.ended_ms >= 0;
        if (o.hold >= 0 || o.shift != 0 || !o.cones || !o.judge) continue;
        // The recordings end as they did on the console, to the millisecond (13 minutes against four cars in lesson 35), but for
        // lessons 2 and 3 (a step later there, ❓) and 26 (recorded with other parts: its file's traction control is not its
        // pool's). Lesson 36's keeps no time.
        static constexpr int kOther[] = {2, 3, 26, 36};
        if (std::find(std::begin(kOther), std::end(kOther), k) == std::end(kOther)) CHECK(r.ended_ms == r.recorded_ms);
        if (k == 2 || k == 3) CHECK(r.recorded_ms - r.ended_ms >= 16 && r.recorded_ms - r.ended_ms <= 17);
        if (k == 36) CHECK(r.ended_ms > 0 && r.fail == 0);
    }
    if (!only) std::printf("  %d end timed, %d to the millisecond\n", ended, exact);
    CHECK(ended > 0);
}

#include "gt4r/race/license_prologue.h"

// Every lesson's rules as the judge reads them, how its run ends and its grade.
TEST(disc_prologue_lesson_rules) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    db::SpecDb db = open_db(*disc);
    gt3r::Volume* vol = &disc->volume();
    db::RaceEvents events(db, [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    });
    const sim::Program program = sim::Program::load(*disc);
    int lessons = 0;
    for (int n = 1; n <= 46; ++n) {
        char label[16];
        std::snprintf(label, sizeof label, "l0p%04d", n);
        const std::optional<db::RaceEvent> e = events.event(label);
        CHECK(e.has_value());
        if (!e) continue;
        ++lessons;
        const race::LicenseRules r = race::LicenseRules::from_row(e->bytes);
        const sim::RaceRules rr = sim::race_rules(e->bytes, program);
        const bool others = r.opponents > 0;
        std::printf("  %s %-18s kind %d laps %d others %d cones %d x41 %d time limit %d mileage %d targets %u %u %u: ends %d, off the course %d\n", label, e->course.c_str(), r.kind, e->laps, r.opponents,
                    r.pylons, r.x41, r.time_limit, rr.mileage, e->target_ms[0], e->target_ms[1], e->target_ms[2], race::prologue::end_mode(r, others),
                    race::prologue::fail_mode(r, others, race::LicenseFail::OffCourse));
        CHECK(r.kind >= 0 && r.kind <= 6);
        CHECK(race::prologue::end_mode(r, others) >= 1 && race::prologue::end_mode(r, others) <= 3);
        // (a run of the gold time takes the gold with no other car ahead; a minute over the last target nothing, or the first
        // one that is not set)
        const std::array<s64, 3> t{s64(e->target_ms[0]), s64(e->target_ms[1]), s64(e->target_ms[2])};
        CHECK(race::prologue::grade(t, t[0], r, 1, 1000, rr.mileage) == 0);
        if (t[0] > 0 && t[1] > 0 && t[2] > 0) CHECK(race::prologue::grade(t, t[2] + 1, r, 1, 1000, rr.mileage) == (r.opponents < 3 ? 3 : 0));
        if (rr.mileage > 0) CHECK(race::prologue::grade(t, t[0], r, 1, rr.mileage - 1, rr.mileage) == 3);
    }
    CHECK(lessons == 46);
    // Lesson 1: two laps, the driver drives on past the line; off the course he coasts down.
    const std::optional<db::RaceEvent> one = events.event("l0p0001");
    if (one) {
        const race::LicenseRules r = race::LicenseRules::from_row(one->bytes);
        CHECK(race::prologue::end_mode(r, false) == 3);
        CHECK(race::prologue::fail_mode(r, false, race::LicenseFail::OffCourse) == 1);
    }
}
