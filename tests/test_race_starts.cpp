// Where a license test's cars start and where its clock starts (race/start_rule.h, race_start.h, RaceFlow), and the
// disc's demonstrations played by our own race.

#include "test_framework.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/race/demonstration_run.h"
#include "gt4r/race/license_rules.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/race/race_start.h"

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
            CHECK(r.ran && r.exact());
        }
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
}
