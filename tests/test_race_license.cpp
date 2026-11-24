// A license test's rules (the RACE row's bytes the race keeps) and the judge that ends the test, on
// made-up steps and on the events of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/race/license_rules.h"
#include "gt4r/race/start_rule.h"

using namespace gt4r;

namespace {

race::LicenseRules rules(int kind, int pylons, int wall, int car) {
    race::LicenseRules r;
    r.kind = kind, r.pylons = pylons, r.wall_limit = wall, r.car_limit = car;
    return r;
}

} // namespace

TEST(race_license_hit_limits) {
    // 0x35FC10: 0 any hit fails, 255 none, else the truncated value at or over the limit.
    CHECK(race::hit_over(0, 0.01f));
    CHECK(!race::hit_over(255, 1e6f));
    CHECK(race::hit_over(8, 8.0f) && !race::hit_over(8, 7.99f));
    CHECK(race::hit_over(40, 40.5f) && !race::hit_over(40, 39.9f));
}

TEST(race_license_judge) {
    // A wall hit under the limit runs on; at the limit it fails with kind 4.
    {
        race::LicenseJudge j(rules(2, 0, 8, 40));
        race::LicenseStep s;
        s.wall_hit = 7.5f;
        CHECK(j.step(s) == race::LicenseFail::None);
        s.wall_hit = 8.2f;
        CHECK(j.step(s) == race::LicenseFail::Hit && j.bits() == 1);
        CHECK(j.step(s) == race::LicenseFail::None); // reported once
    }
    // A car hit: its strength x 100 against the car limit.
    {
        race::LicenseJudge j(rules(2, 0, 8, 40));
        race::LicenseStep s;
        s.car_hit = 0.39f;
        CHECK(j.step(s) == race::LicenseFail::None);
        s.car_hit = 0.41f;
        CHECK(j.step(s) == race::LicenseFail::Hit);
    }
    // A cone where the test counts none fails; where it counts them, they are counted and the count ends it.
    {
        race::LicenseJudge j(rules(1, 0, 0, 40));
        race::LicenseStep s;
        s.cones = 1;
        CHECK(j.step(s) == race::LicenseFail::Hit);
    }
    {
        race::LicenseJudge j(rules(3, 3, 8, 40));
        race::LicenseStep s;
        s.cones = 2;
        CHECK(j.step(s) == race::LicenseFail::None && j.cones() == 2 && !j.cones_done());
        s.cones = 4;
        CHECK(j.step(s) == race::LicenseFail::None && j.cones() == 3 && j.cones_done());
    }
    // Every wheel off the course fails (kind 3), but not on a loose course; three of four run on.
    {
        race::LicenseJudge j(rules(2, 0, 8, 40));
        race::LicenseStep s;
        s.wheels_off = 3;
        CHECK(j.step(s) == race::LicenseFail::None);
        s.wheels_off = 4, s.off_counts = false;
        CHECK(j.step(s) == race::LicenseFail::None);
        s.off_counts = true;
        CHECK(j.step(s) == race::LicenseFail::OffCourse && j.bits() == 2);
    }
    // Every wheel on the pit lane fails with kind 5, before the off-course rule.
    {
        race::LicenseJudge j(rules(2, 0, 8, 40));
        race::LicenseStep s;
        s.wheels_pit = 4, s.wheels_off = 4;
        CHECK(j.step(s) == race::LicenseFail::PitLane && j.bits() == 4);
    }
    // The time limit: counted while asked, started again otherwise; seconds left (-1 before the count).
    {
        race::LicenseRules r = rules(2, 0, 8, 40);
        r.time_limit = 2;
        race::LicenseJudge j(r);
        CHECK(j.seconds_left() == -1);
        for (int i = 0; i < 61; ++i) j.count_time(true);
        CHECK(j.seconds_left() == 1);
        for (int i = 0; i < 100; ++i) j.count_time(true);
        CHECK(j.seconds_left() == 0);
        j.count_time(false);
        CHECK(j.seconds_left() == -1);
    }
}

TEST(race_license_rules_disc) {
    if (test::discs().empty()) SKIP("set GT4R_DISC");
    size_t ran = 0;
    for (const auto& disc : test::discs()) {
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) continue;
        gt3r::Volume* vol = &disc->volume();
        auto read = [vol](const std::string& path) -> std::optional<Bytes> {
            const gt3r::Volume::Entry* e = vol->find(path);
            return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
        };
        db::SpecDb sdb(read, [vol](const std::string& dir) { return vol->list_dir(dir); });
        const db::RaceEvents ev(sdb, read);
        // B-1: a stop in the box, any wall or cone touched fails, a car hit of 40.
        const std::optional<db::RaceEvent> b1 = ev.event("usl0b0001");
        CHECK(b1.has_value());
        if (!b1) continue;
        const race::LicenseRules r1 = race::LicenseRules::from_row(b1->bytes);
        CHECK(r1.kind == 1 && r1.opponents == 0 && r1.pylons == 0 && r1.wall_limit == 0 && r1.car_limit == 40);
        // B-5: a lap behind the pace car (kind 6, one other car), walls of 8 m/s.
        const race::LicenseRules r5 = race::LicenseRules::from_row(ev.event("usl0b0005")->bytes);
        CHECK(r5.kind == 6 && r5.opponents == 1 && r5.wall_limit == 8);
        // The coffee break's cones: 100 to knock down.
        const race::LicenseRules c2 = race::LicenseRules::from_row(ev.event("usl0c0002")->bytes);
        CHECK(c2.kind == 3 && c2.pylons == 100);
        // A race has no test kind.
        CHECK(!race::LicenseRules::from_row(ev.event("usrusa_0500")->bytes).any());
        // The loose courses' kind (COURSE +0x10): Grand Canyon's is 2 or 3, Tsukuba's not.
        const int gc = race::course_start(sdb, "grandcanyon").kind, ts = race::course_start(sdb, "tsukuba").kind;
        CHECK((gc == 2 || gc == 3) && ts != 2 && ts != 3);
        ++ran;
    }
    if (!ran) SKIP("no Gran Turismo 4 US disc");
}
