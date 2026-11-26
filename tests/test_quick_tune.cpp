// Arcade's Quick Tune on the player's car (sim/quick_tune.h) and the standing start's hold an event's row gives (race_flow.h
// row_start), on the disc of GT4R_DISC.

#include "test_framework.h"

#include "gt4r/disc/disc.h"
#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/race/race_flow.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/quick_tune.h"
#include "gt4r/sim/race_tires.h"
#include "gt4r/sim/session.h"
#include "gt4r/sim/showroom.h"

#include <cmath>
#include <cstdio>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

TEST(race_row_start_hold) {
    std::vector<u8> row(151, 0);
    race::StartType t = race::StartType::Rolling;
    int s = -1;
    CHECK(!race::row_start(std::vector<u8>(0x41, 0), t, s) && t == race::StartType::Rolling);
    row[0x40] = 0, row[0x41] = 7;
    CHECK(race::row_start(row, t, s) && t == race::StartType::Given && s == 7 && race::start_hold_steps(t, s) == 420);
    row[0x40] = 1, row[0x41] = 0;
    CHECK(race::row_start(row, t, s) && race::start_hold_steps(t, s) == 480);
    row[0x40] = 2, row[0x41] = 3; // (the type's own seconds: the row's are not read)
    CHECK(race::row_start(row, t, s) && race::start_hold_steps(t, s) == 600);
    row[0x40] = 3;
    CHECK(race::row_start(row, t, s) && race::start_hold_steps(t, s) == 0);
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Volume* vol = &disc->volume();
    const sim::FileReader read = [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
    db::SpecDb db(read, [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    const db::RaceEvents events(db, read);
    // The four machine tests hold 7 s; the arcade's sample race and an event 10 s; a license test that stands 8 s.
    for (const char* label : {"mt400_testcourse", "mt1000_testcourse", "mtmax_testcourse", "mt400_lasvegas"}) {
        const std::optional<db::RaceEvent> e = events.event(label);
        CHECK(e && race::row_start(e->bytes, t, s) && t == race::StartType::Given && race::start_hold_steps(t, s) == 7 * 60);
    }
    for (const char* label : {"sample_entry_6", "sample_timetrial"}) {
        const std::optional<db::RaceEvent> e = events.event(label);
        CHECK(e && race::row_start(e->bytes, t, s) && t == race::StartType::Ten);
    }
    const std::optional<db::RaceEvent> b1 = events.event("usl0b0001");
    CHECK(b1 && race::row_start(b1->bytes, t, s) && t == race::StartType::Eight);
}

TEST(quick_tune_on_the_player_car) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    gt3r::Volume* vol = &disc->volume();
    const sim::FileReader read = [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
    db::SpecDb db(read, [vol](const std::string& dir) { return vol->list_dir(dir); }, "american");
    const sim::Program program = sim::Program::load(*disc);
    const sim::Showroom showroom(db, program);
    sim::Equipment stock;
    CHECK(sim::stock_car(db, program, "ka_01", stock));
    // A stock car with sports medium tires (the arcade's 4, as the race puts them on before the menus).
    CHECK(sim::fit_race_tires(db, read, stock, 0, 4) || sim::tire_kind(db, stock, 0) == 4);
    const std::optional<sim::ShowroomFigures> before = showroom.figures(stock);
    CHECK(before.has_value());
    if (!before) return;

    // Untouched page: the work's values as the sequence fills them change nothing but the flags.
    sim::Equipment same = stock;
    sim::QuickTune none;
    none.front_tire = none.rear_tire = 4;
    sim::apply_quick_tune(db, program, read, same, none, 0);
    CHECK(same.part(sim::Equipment::FrontTire) == stock.part(sim::Equipment::FrontTire) && same.b(sim::Equipment::PowerPct) == 100 &&
          same.b(sim::Equipment::WeightPct) == 100);

    // Power +10 %, weight -10 %, racing soft tires in front and super soft behind.
    sim::QuickTune q;
    q.power_level = 10, q.weight_level = -10, q.front_tire = 8, q.rear_tire = 9;
    sim::Equipment tuned = stock;
    sim::apply_quick_tune(db, program, read, tuned, q, 0);
    CHECK(tuned.b(sim::Equipment::PowerPct) == 110 && tuned.b(sim::Equipment::WeightPct) == 90);
    CHECK(sim::tire_kind(db, tuned, 0) == 8 && sim::tire_kind(db, tuned, 1) == 9);
    const std::optional<sim::ShowroomFigures> after = showroom.figures(tuned);
    CHECK(after.has_value());
    if (!after) return;
    const float ps0 = before->power.host(), ps1 = after->power.host();
    std::printf("  ka_01: %.1f PS %d kg, tuned %.1f PS %d kg\n", double(ps0), int(before->weight), double(ps1), int(after->weight));
    CHECK(std::fabs(ps1 - ps0 * 1.1f) < 1.0f);
    CHECK(after->weight == s16(s32(before->weight) * 90 / 100));
    // On dirt the race's own kind whatever the page said (RaceArcade::vf92).
    sim::Equipment dirt = stock;
    sim::apply_quick_tune(db, program, read, dirt, q, 2);
    CHECK(sim::tire_kind(db, dirt, 0) == 11 && sim::tire_kind(db, dirt, 1) == 11);
    // A level below 0 wraps as the program's byte does: -50 is 50 %.
    sim::Equipment light = stock;
    sim::QuickTune half;
    half.power_level = -50, half.front_tire = half.rear_tire = 4;
    sim::apply_quick_tune(db, program, read, light, half, 0);
    CHECK(light.b(sim::Equipment::PowerPct) == 50);

    // The gearbox's slider: a car whose stock gearbox makes its ratios from it.
    int made = 0, shown = 0;
    for (const db::CarEntry& c : db.cars()) {
        sim::Equipment eq;
        if (!sim::stock_car(db, program, c.label, eq)) continue;
        const sim::GearSliderRange g = sim::quick_tune_gears(db, eq);
        if (g.max == 0) continue;
        ++made;
        if (shown++ == 0) {
            CHECK(g.min <= g.value && g.value <= g.max && g.min < g.max);
            sim::QuickTune gq;
            gq.front_tire = gq.rear_tire = int(sim::tire_kind(db, eq, 0));
            gq.gear_top_speed = g.value == g.max ? g.min : g.max;
            sim::Equipment geared = eq;
            sim::apply_quick_tune(db, program, read, geared, gq, 0);
            CHECK(geared.b(sim::Equipment::GearSlider) == gq.gear_top_speed);
            CHECK(geared.h(sim::Equipment::GearRatios + 2) != eq.h(sim::Equipment::GearRatios + 2));
            std::printf("  %s: slider %d (%d .. %d) -> %d, first gear %.3f -> %.3f\n", c.label.c_str(), g.value, g.min, g.max, gq.gear_top_speed,
                        eq.h(sim::Equipment::GearRatios + 2) / 1000.0, geared.h(sim::Equipment::GearRatios + 2) / 1000.0);
        }
    }
    std::printf("  %d cars as sold have a gearbox the slider sets\n", made);

    // Driven: full throttle from Tsukuba's grid, 8 s; the tuned car is further and faster.
    const std::optional<Bytes> data = read("crs/tsukuba");
    CHECK(data.has_value());
    if (!data) return;
    const course::CourseFile file = course::CourseFile::parse(*data);
    const std::optional<course::Runway> road = course::course_road(file, *data);
    CHECK(road.has_value());
    if (!road) return;
    auto run = [&](const sim::Equipment& eq) {
        std::vector<sim::Entry> entries(1);
        entries[0].equipment = eq, entries[0].grid_place = 0, entries[0].control = 0;
        sim::Session session(db, program, std::make_shared<const course::Runway>(*road), entries, sim::course_row(db, "tsukuba"));
        std::vector<race::CarInput> in(1);
        in[0].throttle = 1;
        for (int k = 0; k < 8 * 60; ++k) session.step_once(in);
        return session.cars()[0].speed * 3.6f;
    };
    const float v0 = run(stock), v1 = run(tuned);
    std::printf("  8 s at full throttle: %.1f km/h, tuned %.1f km/h\n", double(v0), double(v1));
    CHECK(v1 > v0 + 2);
}
