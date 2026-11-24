// Gran Turismo 4 Prologue (docs/formats/prologue.md): its database read in Gran Turismo 4's layout,
// its roads as Runways, its drivers' lines from the course files, its program's tables, and a race
// run on them; on made-up data, then on the Prologue disc of GT4R_DISC.

#include "test_framework.h"

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
    const sim::ai::FieldRun run = sim::ai::run_field(*disc, "tsukuba", {"impreza_wrc_03", "civic_r_98"}, 1);
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
    for (const char* which : {"engine", "prologue"}) {
        std::string err;
        const std::vector<diff::prologue::PTapStat> stats = diff::prologue::check(*disc, "tsukuba", "civic_r_98", 1200, which, err);
        if (!err.empty()) std::printf("  %s\n", err.c_str());
        CHECK(err.empty());
        // (the engine's routines Prologue does not share are tapped too, to show how they differ)
        static const std::set<std::string> differ = {"slip ratios", "tire slip force", "forward step", "lateral and yaw", "wheel steer",
                                                     "attitude", "ground frame", "rolling resistance", "brake heat",
                                                     "engine's attitude, GT4's axes"};
        for (const diff::prologue::PTapStat& st : stats) {
            std::printf("  %s: %s %llu of %llu calls equal\n", which, st.name.c_str(), static_cast<unsigned long long>(st.clean),
                        static_cast<unsigned long long>(st.calls));
            CHECK(st.calls > 0 || st.name == "engine's attitude, GT4's axes");
            if (!differ.count(st.name)) CHECK(st.clean == st.calls);
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
        if (n <= 6) std::printf("  %s: %u / %u / %u ms\n", label, e->target_ms[0], e->target_ms[1], e->target_ms[2]);
    }
    std::printf("  %d of 46 tests have a gold time\n", with_gold);
    CHECK(with_gold == 46);
}
