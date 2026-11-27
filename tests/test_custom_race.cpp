// Quick race's custom race: what Arcade's difficulty is on the disc. A car's own Arcade races (ARCADEINFO_NORMAL through
// MCarData.GetRaceCode) by EASY, NORMAL and HARD carry the same driver percents, and the race of a car without (sample_entry_6)
// sets none: the game has no skill per driver to offer (the custom race's opponents drive as that row has them).

#include "menu_run_flow.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/program.h"

#include <cstdio>

using script::Value;

TEST(disc_arcade_difficulty_sets_no_driver) {
    Disc* disc = gt4_disc();
    if (!disc || !sim::Program::load(*disc).known()) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    script::Vm& vm = f.rt->vm();
    db::SpecDb sdb = sim::ai::open_specdb(*disc);
    const db::RaceEvents events(sdb, [disc](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = disc->volume().find(path);
        return e ? std::optional<Bytes>(disc->volume().read(*e)) : std::nullopt;
    });
    int with = 0, total = 0, differ = 0;
    for (const db::CarEntry& c : sdb.cars()) {
        ++total;
        std::string codes[3];
        bool any = false;
        const char* lv[3] = {"EASY", "NORMAL", "HARD"};
        for (int d = 0; d < 3; ++d) {
            const std::vector<Value> args{Value::string(c.label), Value::integer(0), Value::string(lv[d])};
            const Value r = vm.call(vm.lookup("main::menu::MCarData::GetRaceCode"), args);
            if (!r.is_nil()) codes[d] = r.to_string(), any = true;
        }
        if (!any) continue;
        ++with;
        std::string line;
        bool same = true;
        std::array<u8, 4> t0{}, b0{}, c0{};
        for (int d = 0; d < 3; ++d) {
            const std::optional<db::RaceEvent> e = events.event(codes[d]);
            if (!e) continue;
            if (d == 0) t0 = e->ai_throttle, b0 = e->ai_brake, c0 = e->ai_corner;
            else same = same && t0 == e->ai_throttle && b0 == e->ai_brake && c0 == e->ai_corner;
            char b[200];
            std::snprintf(b, sizeof b, " %s[t%d,%d,%d,%d b%d,%d,%d,%d c%d,%d,%d,%d grid%d start%d band%d]", codes[d].c_str(), e->ai_throttle[0], e->ai_throttle[1], e->ai_throttle[2], e->ai_throttle[3],
                          e->ai_brake[0], e->ai_brake[1], e->ai_brake[2], e->ai_brake[3], e->ai_corner[0], e->ai_corner[1], e->ai_corner[2], e->ai_corner[3], e->grid,
                          e->bytes.size() > 0x40 ? e->bytes[0x40] : -1, e->band_global);
            line += b;
        }
        if (!same) ++differ;
        if (with < 3 || (!same && differ < 6)) std::printf("  %s:%s\n", c.label.c_str(), line.c_str());
    }
    std::printf("  %d of %d cars with Arcade races; %d differ by difficulty\n", with, total, differ);
    CHECK(differ == 0);
    const std::optional<db::RaceEvent> e6 = events.event("sample_entry_6");
    CHECK(e6.has_value());
    if (e6) {
        for (int k = 0; k < 4; ++k) CHECK(e6->ai_throttle[size_t(k)] % 100 == 0 && e6->ai_brake[size_t(k)] % 100 == 0 && e6->ai_corner[size_t(k)] % 100 == 0);
        CHECK(e6->slow_pct == 0 && e6->fast_pct == 0); // (no rubber band)
    }
    if (const std::optional<db::RaceEvent> e = events.event("sample_entry_6"))
        std::printf("  sample_entry_6: grid %d laps %d start %d t%d b%d c%d\n", e->grid, e->laps, e->bytes[0x40], e->ai_throttle[0], e->ai_brake[0], e->ai_corner[0]);
}

TEST(disc_prologue_arcade_lists_explore) {
    Disc* disc = nullptr;
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::Prologue) disc = d;
    if (!disc) SKIP("needs the Prologue disc");
    Flow f(*disc, "JP");
    f.rt->boot();
    script::Vm& vm = f.rt->vm();
    const Value cars = vm.attribute(vm.lookup("main::prologue::ArcadeCarList"), "car_list");
    const Value crs = vm.attribute(vm.lookup("main::prologue::ArcadeCourseList"), "crs_list");
    std::printf("  cars %zu courses %zu dirt %s prefix %s\n", cars.items() ? cars.items()->size() : 0, crs.items() ? crs.items()->size() : 0,
                vm.lookup("main::prologue::DIRT_RACECODE").to_string().c_str(), vm.lookup("main::prologue::DIRT_PREFIX").to_string().c_str());
    if (cars.items() && !cars.items()->empty()) {
        const Value& c = (*cars.items())[0];
        std::printf("  car %s race %s\n", vm.attribute(c, "car_code").to_string().c_str(), vm.attribute(c, "race_code").to_string().c_str());
    }
    if (crs.items())
        for (const Value& c : *crs.items())
            std::printf("  crs %s max %s dirt %s\n", vm.attribute(c, "code").to_string().c_str(), vm.attribute(c, "max_entry").to_string().c_str(), vm.attribute(c, "is_dirt").to_string().c_str());
    std::printf("  difficulty %s enemies %s\n", vm.attribute(vm.lookup("main::game.option"), "difficulty").to_string().c_str(), f.rt->game().option_text("run_with_enemies").c_str());
}
