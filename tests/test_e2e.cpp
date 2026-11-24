// The game played end to end as a player plays it, headless: the menus from power on, the race
// they ask for run for real (the simulation with the computer drivers at the wheel of every car,
// the player's too, as in a B-spec race), its result handed back to the menus, the menus after
// it, and the game saved and loaded. tests/menu_run_flow.h drives the menus.

#include "menu_run_flow.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/ai/run.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <numeric>

namespace {

Disc* e2e_disc() {
    Disc* d = gt4_disc();
    return d && sim::Program::load(*d).known() ? d : nullptr;
}

db::SpecDb::Reader disc_reader(Disc& disc) {
    gt3r::Volume* vol = &disc.volume();
    return [vol](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = vol->find(path);
        return e ? std::optional<Bytes>(vol->read(*e)) : std::nullopt;
    };
}

// A race the menus asked for, run for real: the grid the race screen makes (sim::ai::grid_for:
// the scripts' opponents or the event's pool, the player last), every car with its parts and
// settings (the player's garage car its own), the event's pace, the computer drivers driving.
// The result as the race screen hands it to the menus.
struct RealRace {
    menu::RaceResult result;
    sim::ai::FieldRun run;
    std::vector<std::string> grid; // car labels in grid order
};

RealRace run_for_real(Disc& disc, const menu::GameState& game, const menu::RaceRequest& q, int laps, u32 seed = 1) {
    RealRace out;
    db::SpecDb sdb = sim::ai::open_specdb(disc);
    const sim::Program program = sim::Program::load(disc);
    const db::RaceEvents events(sdb, disc_reader(disc));
    std::vector<sim::ai::FieldCar> cars;
    for (const sim::ai::GridEntry& g : sim::ai::grid_for(events, q, seed)) {
        sim::ai::FieldCar c;
        c.name = g.car;
        bool ok = false;
        if (g.player && q.garage_slot >= 0) {
            if (const std::optional<sim::Equipment> eq = menu::race_equipment(game, q.garage_slot)) c.entry.equipment = *eq, ok = true;
        }
        if (!ok) ok = g.player ? sim::stock_car(sdb, program, g.car, c.entry.equipment) : sim::ai::entry_equipment(sdb, program, events, g, c.entry.equipment);
        if (!ok) throw std::runtime_error("no equipment for " + g.car);
        cars.push_back(std::move(c));
        out.grid.push_back(g.car);
    }
    sim::ai::Pace pace;
    if (const std::optional<db::RaceEvent> e = events.event(q.race)) pace = sim::ai::pace_for(*e);
    std::string course = q.course;
    if (!disc.volume().find("crs/" + course) && course.rfind("r_", 0) == 0) course = course.substr(2); // (as the race screen does)
    out.run = sim::ai::run_field(disc, course, cars, laps, pace);

    // The finishers by time, then the cars that did not finish.
    const size_t player = cars.size() - 1;
    std::vector<size_t> order(cars.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        const sim::ai::CarRun &ra = out.run.cars[a], &rb = out.run.cars[b];
        if (ra.finished != rb.finished) return ra.finished;
        return ra.finished && ra.total < rb.total;
    });
    menu::RaceResult& r = out.result;
    r.race = q.race, r.course = q.course, r.garage_slot = q.garage_slot;
    r.watched = true; // the computer drove the player's car (a B-spec race)
    r.finished = out.run.cars[player].finished;
    for (size_t i = 0; i < order.size(); ++i) {
        const sim::ai::CarRun& c = out.run.cars[order[i]];
        menu::RaceResult::Finisher f;
        f.entry = order[i] == player ? 0 : int(order[i]) + 1;
        f.car = out.grid[order[i]];
        f.finished = c.finished;
        f.time_ms = c.finished ? u32(c.total * 1000.0) : menu::kNoTime;
        if (order[i] == player) r.place = int(i), r.time_ms = c.finished ? f.time_ms : 0;
        r.order.push_back(f);
    }
    r.distance_m = out.run.lap_length * float(out.run.cars[player].laps.size());
    return out;
}

void print_run(const RealRace& race) {
    for (size_t i = 0; i < race.result.order.size(); ++i) {
        const menu::RaceResult::Finisher& f = race.result.order[i];
        std::printf("  %zu. %-30s %s%s\n", i + 1, f.car.c_str(), f.finished ? menu::time_text(f.time_ms).c_str() : "did not finish",
                    f.entry == 0 ? "  (player)" : "");
    }
}

// The prize page counts the prize into the credits; the results page's Exit ends the sequence.
bool leave_race_menus(Flow& f, Sequence& q) {
    f.rt->start_project(q.project);
    f.rt->settle();
    if (f.page() == "PrizeRoot") {
        for (int i = 0; i < 8000; ++i) {
            const std::shared_ptr<menu::LiveWidget> ok = f.rt->current_page() ? f.rt->current_page()->child("OK") : nullptr;
            if (ok && ok->visible() && f.focus() == "OK") break;
            f.rt->update();
        }
        f.rt->settle();
        f.press({menu::Key::Accept});
    }
    if (f.page() != "AfterRoot" || !f.focus_on("Exit")) return false;
    f.press({menu::Key::Accept});
    return f.rt->finished() && q.work().take_command() == "COMMAND_EXIT";
}

} // namespace

// Arcade: the title, Single Race at Tsukuba in the Ford Ka, the race the menus ask for run for
// real with the field the race screen makes (no car named "nil": the scripts' entry 0 is the
// player's), every car finishing; the race's menus after it, and Arcade again.
TEST(disc_e2e_arcade_single_race) {
    Disc* disc = e2e_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    // Arcade Mode, Single Race, World Circuits, Tsukuba, Ford, the Ka, its first paint, OK.
    f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept, menu::Key::Right, menu::Key::Right, menu::Key::Accept, menu::Key::Down,
             menu::Key::Accept, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
    const std::optional<menu::RaceRequest> race = f.rt->race_request();
    CHECK(race && race->sequence == "single_race" && race->course == "tsukuba" && race->car == "ka_01");
    if (!race) return;
    menu::RaceRequest q = *race;
    // The race screen's grid: opponents that are cars, the player last.
    db::SpecDb sdb = sim::ai::open_specdb(*disc);
    const db::RaceEvents events(sdb, disc_reader(*disc));
    const std::vector<sim::ai::GridEntry> grid = sim::ai::grid_for(events, q, 1);
    CHECK(grid.size() == size_t(q.max_entry) && grid.back().player && grid.back().car == "ka_01");
    for (const sim::ai::GridEntry& g : grid) CHECK(!g.car.empty() && g.car != "nil");

    // The race's menus before it: Start.
    Sequence s(f, q);
    s.fill(q.max_entry);
    CHECK(s.menus({menu::Key::Accept}) == "COMMAND_START");
    const RealRace real = run_for_real(*disc, f.rt->game(), q, 1);
    print_run(real);
    for (const sim::ai::CarRun& c : real.run.cars) CHECK(c.finished);
    CHECK(real.result.finished && real.result.order.size() == grid.size());
    // (A single race keeps its course record only: nothing for race_finished.) The menus after it.
    s.work().set("IsSessionFinished", 1), s.work().set("canReplay", 1);
    f.rt->game().during_quick_menu = true;
    f.rt->start_project(s.project);
    f.rt->settle();
    // The pre-race menu again, with the race run (its Replay icon there); Exit leaves for Arcade.
    CHECK(!f.rt->finished() && f.page() == "ArcadeRoot");
    CHECK(f.focus_on("Replay") && f.focus() == "Replay");
    CHECK(f.focus_on("Exit"));
    f.press({menu::Key::Accept});
    CHECK(f.rt->finished());
    CHECK(s.work().take_command() == "COMMAND_EXIT");
    CHECK(s.leave() && f.rt->project()->name() == "arcade");
    std::printf("  back in %s on %s\n", f.rt->project()->name().c_str(), f.page().c_str());
    CHECK(f.clean());
}

// Gran Turismo mode: a new game, a used car bought, the Sunday Cup's first race run for real with
// that car against the event's drawn field; the result handed back (place, prize, the day gone
// by), the prize page and the results, the map (the game saves itself), and the next start loads
// the game as it was.
TEST(disc_e2e_gt_mode_real_race) {
    Disc* disc = e2e_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string dir = (std::filesystem::temp_directory_path() / "gt4r-test-e2e").string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    s64 money = 0;
    u32 today = 0;
    size_t diary = 0;
    {
        Flow f(*disc, "US", dir);
        CHECK(f.boot_to_title());
        CHECK(!f.new_game().empty());
        menu::GameState& g = f.rt->game();
        // The used car lot's first car.
        f.map_focus("UsedCarAsia");
        f.press({menu::Key::Accept});
        f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Right, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        for (int i = 0; i < 4 && f.page() != "TopRoot"; ++i) f.press({menu::Key::Cancel});
        CHECK(f.page() == "TopRoot" && g.garage_count() == 1);
        if (g.garage_count() != 1) return;
        std::printf("  bought %s, %lld credits left\n", g.garage[0].car.c_str(), static_cast<long long>(g.money));
        // The Sunday Cup's first race.
        f.map_focus("BeginnerHall");
        f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        const std::optional<menu::RaceRequest> race = f.rt->race_request();
        CHECK(race && race->sequence == "solo_event" && race->race == "am_sunday_0001" && race->garage_slot == 0);
        if (!race) return;
        Sequence s(f, *race);
        s.fill();
        CHECK(s.menus({menu::Key::Accept}) == "COMMAND_START");
        const RealRace real = run_for_real(*disc, g, *race, 2);
        print_run(real);
        CHECK(real.result.finished && real.grid.size() >= 2 && real.grid.back() == race->car);
        // Handed back as the race screen hands it.
        money = g.money, today = g.today, diary = g.diary.size();
        const s64 prize = s.ran(real.result);
        std::printf("  place %d, prize %lld\n", real.result.place + 1, static_cast<long long>(prize));
        CHECK(prize >= 0);
        CHECK(leave_race_menus(f, s));
        CHECK(g.money == money + prize && g.today == today + 1 && g.diary.size() == diary + 1);
        CHECK(s.leave() && f.rt->project()->name() == "gtmode");
        // Back to the map: the game saves itself.
        for (int i = 0; i < 4 && f.page() != "TopRoot"; ++i) f.press({menu::Key::Cancel});
        CHECK(f.page() == "TopRoot" && !g.need_save);
        money = g.money, today = g.today, diary = g.diary.size();
        CHECK(f.clean());
    }
    {
        // Quit and start again: the game loads as it was saved, in Gran Turismo mode.
        Flow f(*disc, "US", dir);
        f.rt->boot();
        f.rt->start_project(f.rt->first_project());
        f.rt->settle();
        for (int i = 0; i < 6 && !f.rt->finished(); ++i) f.press({f.page() == "MovieRoot" ? menu::Key::Start : menu::Key::Accept});
        const menu::GameState& g = f.rt->game();
        CHECK(f.rt->finished() && g.money == money && g.today == today && g.diary.size() == diary && g.garage_count() == 1);
        CHECK(f.rt->advance());
        f.rt->settle();
        CHECK(f.rt->project()->name() == "gtmode" && f.page() == "TopRoot");
        CHECK(f.clean());
    }
    if (!std::getenv("GT4R_KEEP_TEST_FILES")) std::filesystem::remove_all(dir);
}
