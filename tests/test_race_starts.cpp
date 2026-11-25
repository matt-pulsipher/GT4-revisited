// Where a license test's cars start and where its clock starts (race/start_rule.h, race_start.h, RaceFlow), and the
// disc's demonstrations played by our own race.

#include "test_framework.h"

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/race/demonstration_run.h"
#include "gt4r/race/license_rules.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/race_start.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"

#include <cmath>
#include <cstdio>

using namespace gt4r;

namespace {

void put16(std::vector<u8>& row, size_t o, int v) { row[o] = u8(v), row[o + 1] = u8(v >> 8); }

} // namespace

TEST(race_start_rows) {
    // A row of a moving test: 200 m before the section's start at 90 km/h.
    std::vector<u8> row(151, 0);
    put16(row, 0x14, -200), row[0x42] = 90;
    put16(row, 0x2E, 50), row[0x80] = 0; // (place 1: 50 m on, no speed of its own)
    const race::RowStart r = race::RowStart::from_row(row);
    CHECK(r.moving(race::Conductor::License) && r.moving(race::Conductor::Mission) && !r.moving(race::Conductor::Other));
    const race::CarStart a = race::car_start(r, race::Conductor::License, 0, 1900);
    CHECK(a.kind == 2 && a.distance == 1700 && a.speed == 90 && a.mark == 1);
    // The license's conductor reads place 0's for every car; a mission's the car's own, the speed of place 0 where it has none.
    CHECK(race::car_start(r, race::Conductor::License, 1, 1900).distance == 1700);
    const race::CarStart b = race::car_start(r, race::Conductor::Mission, 1, 1900);
    CHECK(b.kind == 1 && b.distance == 1950 && b.speed == 90 && b.mark == 2);
    // A place of 0 m is the grid's, with the speed all the same (0x34D0F8): the car's own, else place 0's.
    put16(row, 0x2E, 0);
    const race::CarStart still = race::car_start(race::RowStart::from_row(row), race::Conductor::Mission, 1, 1900);
    CHECK(still.kind == 0 && still.speed == 90 && still.mark == 0);
    row[0x80] = 65;
    CHECK(race::car_start(race::RowStart::from_row(row), race::Conductor::Mission, 1, 1900).speed == 65);
    // Without a section: from the line, no mark.
    const race::CarStart c = race::car_start(r, race::Conductor::License, 0, 0);
    CHECK(c.kind == 2 && c.distance == -200 && c.mark == 0);
    // No place of its own: the grid.
    const race::RowStart none = race::RowStart::from_row(std::vector<u8>(151, 0));
    CHECK(!race::car_start(none, race::Conductor::License, 0, 1900).on_line() && !none.moving(race::Conductor::License));
    CHECK(race::conductor_for("license", "l0b0005") == race::Conductor::Training && race::conductor_for("license", "lia0011") == race::Conductor::License &&
          race::conductor_for("mission", "m0001") == race::Conductor::Mission && race::conductor_for("single_race", "") == race::Conductor::Other);
    CHECK(race::section_start_metres(1900.7f, 3000) == 1900 && race::section_start_metres(0.5f, 3000) == 0 && race::section_start_metres(2999.5f, 3000) == 0);
}

// A section test that starts moving: its clock starts where the car passes the section's start, the run ends at the goal.
TEST(race_flow_section_test) {
    race::FlowSetup fs;
    fs.start = race::StartType::Moving, fs.laps = 1, fs.lap_length = 3000, fs.counted = true;
    fs.section_start = 1900, fs.goal = 2500, fs.test_kind = 2;
    fs.crossings = {1}, fs.marks = {1};
    std::vector<race::CarState> cars(1);
    cars[0].course_v = 1704;
    race::RaceFlow flow(fs, cars);
    int start_step = -1, end_step = -1;
    for (int i = 0; i < 400 && end_step < 0; ++i) {
        cars[0].course_v += 10;
        const race::RaceFlow::Events e = flow.step(cars);
        if (e.start) {
            start_step = i;
            CHECK(e.started.size() == 1 && e.started[0] == 0);
        }
        if (e.player_finished) end_step = i;
        if (start_step < 0) CHECK(flow.lap_ms(0) == 0);
    }
    // 1704 .. 1904 passes 1900 in step 19, six tenths in; 2494 .. 2504 passes 2500 in step 79, six tenths in: 60 steps.
    CHECK(start_step == 19 && end_step == 79);
    CHECK(flow.run_ms(0) == 1000);
    // A test of kind 3 is not ended by the goal; a car going backwards over the line does not count it.
    fs.test_kind = 3, fs.marks = {0}, fs.goal = -1, fs.section_start = -1, fs.crossings = {0};
    cars[0].course_v = 2990;
    race::RaceFlow lap(fs, cars);
    cars[0].course_v = 4, lap.step(cars);
    CHECK(lap.car(0).counter == 1 && lap.steps_since_start() == 0);
    cars[0].course_v = 2996, lap.step(cars);
    CHECK(lap.car(0).counter == 1 && lap.car(0).back == 1);
    cars[0].course_v = 8, lap.step(cars);
    CHECK(lap.car(0).counter == 1 && lap.car(0).back == 0 && !lap.player_finished());
    for (int i = 0; i < 300 && !lap.player_finished(); ++i) {
        cars[0].course_v += 11;
        if (cars[0].course_v >= 3000) cars[0].course_v -= 3000;
        lap.step(cars);
    }
    CHECK(lap.player_finished() && lap.car(0).counter == 2);
}

// Demonstrations played by our own race end on their recorded times: a stop in the box (B-1), a section with cones passed at speed (B-4),
// moving section starts, before the section (A-1) and by its driver up to it (A-14).
TEST(race_demonstrations_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    bool ran = false;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        ran = true;
        gt3r::Volume* vol = &disc->volume();
        db::SpecDb sdb([vol](const std::string& p) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(p);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        }, [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
        for (const char* label : {"usl0b0001", "usl0b0004", "usl0a0001", "usl0a0014"}) {
            const race::DemonstrationRun r = race::run_demonstration(*disc, sdb, label);
            std::printf("  %s: recorded %d ms, ours %d ms (failed %d) %s\n", label, r.recorded_ms, r.ended_ms, r.fail, r.error.c_str());
            CHECK(r.ran && r.fail == 0 && r.ended_ms > 0 && (r.recorded_ms == 0 || r.exact())); // (B-3's recording keeps no time)
        }
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
}

TEST(race_driving_mode_changed_in_the_race) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The PIT STOP menu's driving mode (0x3F19E0 command 10): a pad's car standing on its brake, given to the B-spec driver, drives
    // on along the road; given back, it stops on its pad's brake.
    const std::vector<std::string> labels{"ka_01", "s500_63"};
    auto session = sim::Session::open(*disc, "tsukuba", labels);
    std::vector<size_t> cars{0, 1};
    auto field = sim::ai::Field::open(*disc, "tsukuba", *session, cars);
    field->set_shadow(0);
    std::vector<race::CarInput> in(2);
    for (int k = 0; k < sim::Session::kHoldSteps; ++k) field->step(*session, in, -1, u32(sim::Session::kHoldSteps - k));
    auto run = [&](int steps) {
        float run_m = 0;
        for (int k = 0; k < steps; ++k) {
            in[0] = {}, in[0].brake = 1;
            field->step(*session, in, -1, 0);
            run_m += session->cars()[0].speed * sim::Session::kStep;
        }
        return run_m;
    };
    CHECK(std::fabs(run(300)) < 1);
    CHECK(field->control(0) == 0);
    field->set_control(0, 1);
    const float driven = run(900);
    CHECK(driven > 150 && session->cars()[0].speed > 10);
    field->set_control(0, 0);
    run(600);
    CHECK(std::fabs(session->cars()[0].speed) < 0.1f);
    std::printf("  %.1f m driven by the B-spec driver\n", double(driven));
}

TEST(race_mission_rival_moves_from_the_grid) {
    Disc* disc = nullptr;
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) disc = d.get();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // Missions 2 .. 5 and 8: the rival's place is 0 m, the section's grid, and it moves from there at its own speed or the
    // player's, driven by its driver from the first step; the player comes from behind at the row's.
    gt3r::Volume* vol = &disc->volume();
    const auto read = [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
    db::SpecDb db(read, [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    const sim::Program program = sim::Program::load(*disc);
    const db::RaceEvents events(db, read);
    const std::pair<const char*, int> kRivals[] = {{"usl0m0002", 80}, {"usl0m0003", 70}, {"usl0m0004", 100}, {"usl0m0005", 90}, {"usl0m0008", 65}};
    for (const auto& [label, kmh] : kRivals) {
        const std::optional<db::RaceEvent> ev = events.event(label);
        CHECK(ev.has_value());
        if (!ev) continue;
        const std::optional<Bytes> data = read("crs/" + ev->course);
        CHECK(data.has_value());
        if (!data) continue;
        const course::CourseFile file = course::CourseFile::parse(*data);
        const std::optional<course::Runway> road = course::course_road(file, *data);
        CHECK(road.has_value());
        if (!road) continue;
        race::StartCourse starts(*road, file.section(*data, course::kSlotRunway), race::course_start(db, ev->course).way);
        const race::StartRace& r = starts.set_race(race::Conductor::Mission, ev->bytes, race::LicenseRules::from_row(ev->bytes).section);
        CHECK(starts.may_need_lines(2));
        if (const std::optional<course::AiData> ad = sim::ai::Field::read(*disc, ev->course)) starts.add_lines(*ad, sim::ai::Tables::load(program));
        const std::vector<db::Opponent> pool = events.pool(*ev);
        std::vector<sim::Entry> entries(2);
        bool cars = pool.size() == 2;
        for (size_t i = 0; i < 2 && cars; ++i) {
            cars = sim::stock_car(db, program, pool[i].car, entries[i].equipment);
            entries[i].grid_place = i == 0 ? 1 : 0, entries[i].control = i == 0 ? 0 : 2;
            race::put_start(entries[i], r, race::plan_start(r, i, entries[i].grid_place), i == 0, entries[i].grid_place, program);
        }
        CHECK(cars);
        if (!cars) continue;
        sim::Session session(db, program, std::make_shared<const course::Runway>(*road), entries, sim::course_row(db, ev->course));
        const race::CarState &me = session.cars()[0], &rival = session.cars()[1];
        std::printf("  %s: the rival %.1f km/h at %.1f m, the player %.1f km/h at %.1f m\n", label, double(rival.speed * 3.6f), double(rival.course_v),
                    double(me.speed * 3.6f), double(me.course_v));
        CHECK(std::fabs(rival.speed * 3.6f - float(kmh)) < 0.5f && !rival.rolling);
        CHECK(std::fabs(me.speed * 3.6f - float(ev->bytes[0x42])) < 0.5f && me.rolling);
        float ahead = rival.course_v - me.course_v;
        if (ahead < -road->length / 2) ahead += road->length;
        CHECK(ahead > 5 && ahead < 120);
    }
}
