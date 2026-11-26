// Arcade's 2 Player Battle (race/split_battle.h): its rules from the options, the bytes its conductor puts into the race's row,
// the handicap's starts, the screen's halves, who is told which gap, the halves' layouts of the program, and two cars driven by
// two pads on one session with the battle's rubber band and finish.

#include "test_framework.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway_gtrw.h"
#include "gt4r/formats/db/race_events.h"
#include "gt4r/race/race_display.h"
#include "gt4r/race/split_battle.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_wear.h"
#include "gt4r/sim/session.h"

#include "disc/core_image.h"

#include <array>
#include <cmath>

using namespace gt4r;

namespace {

Disc* split_disc() {
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d;
    return nullptr;
}

} // namespace

TEST(split_rules_from_options) {
    std::map<std::string, std::variant<s32, float, std::string>> options;
    race::SplitRules r = race::SplitRules::from_options(options);
    CHECK(r.laps == 2 && r.tire_damage == 0 && r.handicap == 0 && r.boost == 1); // a new game's
    options["split_race_laps"] = s32(5), options["split_tire_damage"] = s32(2), options["split_handicap"] = s32(-35), options["split_boost_level"] = s32(0);
    options["arcade_laps"] = s32(9);
    r = race::SplitRules::from_options(options);
    CHECK(r.laps == 5 && r.tire_damage == 2 && r.handicap == -35 && r.boost == 0);
}

TEST(split_handicap_starts) {
    CHECK(!race::handicap_start(0, 0) && !race::handicap_start(0, 1));
    const auto at = [](int h, size_t car) { return race::handicap_start(h, car)->distance; };
    CHECK(at(50, 0) == -10 && at(50, 1) == -50);
    CHECK(at(5, 0) == -10 && at(5, 1) == -20); // (the car behind never nearer than 20 m before the line)
    CHECK(at(-50, 0) == -50 && at(-50, 1) == -10);
    CHECK(at(-5, 0) == -20 && at(-5, 1) == -10);
    CHECK(race::handicap_start(50, 0)->kind == 1 && race::handicap_start(50, 0)->speed == 0 && race::handicap_start(50, 1)->on_line());
}

TEST(split_tires) {
    CHECK(race::split_tire(0, 4) == 4 && race::split_tire(1, -1) == -1);
    CHECK(race::split_tire(2, 4) == 11 && race::split_tire(3, 4) == 12 && race::split_tire(2, -1) == 11);
}

TEST(split_views) {
    const race::SplitView top = race::split_view(false, 0), low = race::split_view(false, 1), side = race::split_view(true, 1);
    CHECK(top.lens == 1.2f && side.lens == 1.0f && top.picture[1] * 2 == 448 && side.picture[0] * 2 == 640);
    // The mirrors lie in their halves, in the middle.
    for (const race::SplitView* v : {&top, &low, &side}) {
        CHECK(v->mirror[1] > 0 && v->mirror[1] + v->mirror[3] < v->picture[1]);
        CHECK(v->mirror[0] * 2 + v->mirror[2] == v->picture[0]);
    }
    CHECK(top.mirror[1] == low.mirror[1] + 8);
}

TEST(split_gaps) {
    race::ArrivalBoard b;
    b.reset(3);
    std::array<int, 2> place{1, 2};
    const auto tell = [&] { return b.tell([&](size_t car) { return place[car]; }); };
    // The first arrival: nobody is told.
    b.arrive(0, 0, 0, 10000);
    CHECK(tell().gaps.empty());
    // The second: told how far behind, and the first how far ahead.
    b.arrive(1, 0, 0, 10750);
    race::ArrivalBoard::Told t = tell();
    CHECK(t.gaps.size() == 2 && t.gaps[0].car == 1 && t.gaps[0].ms == 750 && t.gaps[1].car == 0 && t.gaps[1].ms == -750);
    CHECK(tell().gaps.empty()); // (once)
    // The second arrival is the race's leader by now (it passed right after the point): it is not told of a gap behind, and the
    // other, whose arrival was the first, is told no lead over the race's second.
    b.arrive(0, 0, 1, 20000);
    tell();
    place = {2, 1};
    b.arrive(1, 0, 1, 20100);
    t = tell();
    CHECK(t.gaps.empty() || (t.gaps.size() == 1 && t.gaps[0].car == 1));
    // Both in one step: by their times.
    place = {2, 1};
    b.arrive(0, 0, 2, 30020), b.arrive(1, 0, 2, 30010);
    t = tell();
    CHECK(t.gaps.size() == 2 && t.gaps[0].car == 0 && t.gaps[0].ms == 10 && t.gaps[1].car == 1 && t.gaps[1].ms == -10);
    // A new lap starts the point again; a car a lap down is told by how many laps.
    b.arrive(1, 1, 0, 60000);
    CHECK(tell().gaps.empty());
    b.arrive(1, 2, 0, 90000), tell();
    b.arrive(0, 1, 0, 90500);
    t = tell();
    CHECK(t.gaps.empty() && t.laps_down.size() == 1 && t.laps_down[0].car == 0 && t.laps_down[0].laps == 1);
    b.arrive(0, 0, 7, 1); // (a point the board does not have)
    CHECK(tell().gaps.empty());
}

TEST(disc_split_rules_bytes) {
    Disc* disc = split_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program program = sim::Program::load(*disc);
    db::SpecDb sdb = sim::ai::open_specdb(*disc);
    gt3r::Volume* vol = &disc->volume();
    const db::RaceEvents events(sdb, [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    });
    const std::optional<db::RaceEvent> sample = events.event("sample_entry_1");
    CHECK(sample && sample->bytes.size() > 0x74);
    if (!sample || sample->bytes.size() <= 0x74) return;
    CHECK(sample->bytes[0x40] == 2 && sample->bytes[0x41] == 0); // a standing start, its type's 10 s
    const auto wear = [&](int level) {
        std::vector<u8> row = sample->bytes;
        race::put_tire_damage(row, level);
        u8 block[0x80] = {};
        sim::rules::event_wear_bytes(row.data(), block);
        const auto b = sim::rules::wear_rules_block(block, true, program);
        return sim::rules::wear_rules(b.data());
    };
    const sim::rules::WearRules none = wear(0), low = wear(1), high = wear(2);
    CHECK(none.tread_on == 0 && low.tread_on == 1 && high.tread_on == 1);
    // The stronger damage reaches its first knee sooner, and loses no less there.
    CHECK(high.tread_knee1.host() < low.tread_knee1.host());
    CHECK(wear(3).tread_on == 0); // (any other level: none)
    // The boost's bytes as the drivers read them.
    const auto pace = [&](int level) {
        db::RaceEvent e = *sample;
        race::put_boost(e.bytes, level);
        return std::array<int, 4>{e.bytes[0x64], e.bytes[0x68], e.bytes[0x69], e.bytes[0x6A]};
    };
    CHECK((pace(0) == std::array<int, 4>{0xFF, 0, 0, 0}) && (pace(1) == std::array<int, 4>{0, 7, 20, 20}) && (pace(2) == std::array<int, 4>{0, 20, 10, 20}));
    std::vector<u8> short_row(8);
    race::put_boost(short_row, 1), race::put_tire_damage(short_row, 1);
    CHECK(short_row == std::vector<u8>(8));
}

TEST(disc_start_types) {
    Disc* disc = split_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = sim::ai::open_specdb(*disc);
    gt3r::Volume* vol = &disc->volume();
    const db::RaceEvents events(sdb, [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    });
    // The rows' start types and seconds (+0x40, +0x41; 0 seconds: the type's own): the races 2, the license tests held 1, the
    // moving starts 3, the machine tests 0 with 7 s.
    std::map<std::pair<int, int>, int> kinds;
    const db::Labels* l = sdb.labels("RACE");
    CHECK(l != nullptr);
    if (!l) return;
    for (const auto& [label, id] : l->entries)
        if (const std::optional<db::RaceEvent> e = events.event(label); e && e->bytes.size() > 0x41) ++kinds[{e->bytes[0x40], e->bytes[0x41]}];
    for (const auto& [k, n] : kinds) std::printf("  start type %d, %d s: %d events\n", k.first, k.second, n);
    CHECK((kinds[{2, 0}] > 500 && kinds[{1, 0}] > 0 && kinds[{3, 0}] > 0 && kinds[{0, 7}] > 0 && kinds.size() == 4));
}

TEST(disc_split_layouts) {
    size_t ran = 0;
    const std::vector<u8> zeros(0x700000);
    CHECK(!race::read_split_layout(zeros, 0x100000, false).valid() && !race::read_split_layout(zeros, 0x100000, true).valid());
    for (const auto& disc : test::reference_discs()) {
        const gt3r::CoreImage core = gt3r::CoreImage::decode(disc->read_program());
        if (disc->info().serial.rfind("SCUS_973.28", 0) != 0) {
            CHECK(!race::read_split_layout(core.program, core.load_address, false).valid());
            continue;
        }
        ++ran;
        const race::DisplayLayout stacked = race::read_split_layout(core.program, core.load_address, false);
        const race::DisplayLayout side = race::read_split_layout(core.program, core.load_address, true);
        CHECK(stacked.valid() && side.valid());
        // The halves write no lap count; side by side the word "Position" stands over the place, stacked there is none.
        CHECK(side.find(race::element::kPositionWord) && !stacked.find(race::element::kPositionWord));
        for (const race::DisplayLayout* l : {&stacked, &side}) {
            for (const u32 member : {race::element::kPosition, race::element::kMap, race::element::kLapTime, race::element::kBestTime, race::element::kLastTime}) CHECK(l->find(member) != nullptr);
            CHECK(!l->find(race::element::kLapCount));
        }
        // Stacked: on a canvas 240 units high.
        for (const race::DisplayPlace& p : stacked.places)
            if (p.member == race::element::kPosition || p.member == race::element::kLapTime) CHECK(p.y > 0 && p.y < 240);
    }
    if (!ran) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
}

// Two cars, two pads, the battle's rules: each pad drives its own car; the car behind is hurried by the strong boost, whichever
// player's it is, and nothing with the boost off; a car past its finish goes to its driver (mode 3) and the other stays its
// player's.
TEST(disc_split_two_pads) {
    Disc* disc = split_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    for (const int boost : {0, 2})
        for (const size_t leader : {size_t(0), size_t(1)}) {
            sim::ai::Pace pace;
            std::vector<u8> row(0xC0);
            race::put_boost(row, boost);
            pace.band_mode = row[0x64], pace.fast_pct = row[0x68], pace.fast_start_m = row[0x69], pace.fast_full_10m = row[0x6A];
            db::SpecDb sdb = sim::ai::open_specdb(*disc);
            const sim::Program program = sim::Program::load(*disc);
            gt3r::Volume& vol = disc->volume();
            const Bytes data = vol.read(*vol.find("crs/testcourse"));
            const course::CourseFile file = course::CourseFile::parse(data);
            std::optional<course::Runway> road = course::course_road(file, data);
            CHECK(road.has_value());
            if (!road) return;
            std::vector<sim::Entry> entries(2);
            for (size_t i = 0; i < 2; ++i) {
                CHECK(sim::stock_car(sdb, program, i ? "focus_st170_jp_03" : "ka_01", entries[i].equipment));
                entries[i].grid_place = int(i), entries[i].control = 0;
            }
            sim::Session::Options o;
            o.hold_steps = 0;
            sim::Session session(sdb, program, std::make_shared<const course::Runway>(std::move(*road)), entries, sim::course_row(sdb, "testcourse"), &o);
            const std::unique_ptr<sim::ai::Field> field = sim::ai::Field::open(*disc, "testcourse", session, std::array<size_t, 2>{0, 1}, pace);
            CHECK(field != nullptr);
            if (!field) return;
            for (size_t i = 0; i < 2; ++i) field->set_shadow(i);
            std::vector<race::CarInput> in(2);
            const race::Vec3 from[2] = {session.cars()[0].position, session.cars()[1].position};
            float behind = 0, ahead = 0;
            // The leader's pad goes first: the other stands while it is held.
            for (int step = 0; step < 900; ++step) {
                for (size_t i = 0; i < 2; ++i) in[i] = {};
                in[leader].throttle = 1;
                if (step >= 600) in[1 - leader].throttle = 1;
                field->step(session, in);
                behind = std::max(behind, field->time_scale(1 - leader)), ahead = std::max(ahead, step >= 300 ? field->time_scale(leader) : 1.0f);
                if (step == 599) {
                    const auto moved = [&](size_t i) {
                        const race::Vec3 p = session.cars()[i].position;
                        return std::hypot(p.x - from[i].x, p.z - from[i].z);
                    };
                    CHECK(moved(leader) > 100 && moved(1 - leader) < 5);
                }
            }
            std::printf("  boost %d, player %zu ahead: the time scales %.3f ahead, %.3f behind\n", boost, leader + 1, double(ahead), double(behind));
            CHECK(ahead == 1.0f);
            if (boost == 2) CHECK(behind > 1.1f && behind <= 1.2001f);
            else CHECK(behind == 1.0f);
            CHECK(session.cars()[1 - leader].speed > 5);
            // The leader's finish: its driver cruises; the other's pad goes on.
            field->finish_run(session, leader);
            CHECK(field->kind(leader) == 5 && field->kind(1 - leader) == 0);
            for (int step = 0; step < 1200; ++step) {
                for (size_t i = 0; i < 2; ++i) in[i] = {};
                in[leader].throttle = 1; // (its pad does nothing now)
                in[1 - leader].throttle = 1;
                field->step(session, in);
            }
            CHECK(session.cars()[leader].speed < 100 / 3.6f);
            CHECK(std::fabs(field->time_scale(leader) - 1) < 1e-5f);
            CHECK(session.cars()[1 - leader].speed > 15);
        }
}
