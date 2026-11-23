// Gran Turismo mode played through with the game's own menus: a new game, a car, an event
// entered and raced (a made-up result standing in for the race), the prize paid, the day gone
// by, the game saved and loaded, the next race; a championship, a license, a mission, a prize
// car. tests/menu_run_flow.h runs the menus and the race sequence's part around a race.

#include "menu_run_flow.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>

using script::Value;

namespace {

std::string pages(Flow& f) {
    std::string out;
    for (const auto& p : f.rt->open_pages()) out += p->name + " ";
    return out + "[" + f.focus() + "]";
}

// The race's menus after a race: the prize page counts the prize into the credits, then the
// results page; Exit ends the sequence. True when it ended with Exit.
bool after_race(Flow& f, Sequence& q) {
    f.rt->start_project(q.project);
    f.rt->settle();
    if (f.page() == "PrizeRoot") {
        for (int i = 0; i < 1200 && f.focus() != "OK"; ++i) f.rt->update();
        f.rt->settle();
        f.press({menu::Key::Accept});
    }
    if (f.page() != "AfterRoot" || !f.focus_on("Exit")) return false;
    f.press({menu::Key::Accept});
    return f.rt->finished() && q.work().take_command() == "COMMAND_EXIT";
}

// The event hall's first event (the Sunday Cup) and its race `course` (0: the first), entered
// from the map: the race asked for.
std::optional<menu::RaceRequest> enter_sunday_cup(Flow& f, int course = 0) {
    f.map_focus("BeginnerHall");
    f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
    for (int i = 0; i < course; ++i) f.press({menu::Key::Right});
    f.press({menu::Key::Accept, menu::Key::Accept});
    return f.rt->race_request();
}

} // namespace

// A new game, a used car, the Sunday Cup's first race won: the prize counted into the credits on
// the prize page, the race's record, a day gone by; back on the map the game saves itself; the
// next start loads it, and the Sunday Cup's next race is entered with the same car.
TEST(disc_gt_mode_loop_event) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::string dir = (std::filesystem::temp_directory_path() / "gt4r-menu-test-loop").string();
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    s64 money = 0;
    u32 today = 0;
    std::string car;
    {
        Flow f(*disc, "US", dir);
        CHECK(f.boot_to_title());
        CHECK(!f.new_game().empty());
        menu::GameState& g = f.rt->game();
        CHECK(g.money == 10'000 && g.garage_count() == 0);
        // A used car: the lot's first, its price paid, driven from now on.
        f.map_focus("UsedCarAsia");
        f.press({menu::Key::Accept});
        CHECK(f.page() == "UsedCarRoot");
        f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Right, menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        for (int i = 0; i < 4 && f.page() != "TopRoot"; ++i) f.press({menu::Key::Cancel});
        CHECK(f.page() == "TopRoot" && g.garage_count() == 1 && g.riding_car == 0 && g.money < 10'000);
        if (g.garage_count() != 1) return;
        car = g.garage[0].car;
        std::printf("  bought %s, %lld credits left\n", car.c_str(), static_cast<long long>(g.money));
        // The Sunday Cup's first race with that car.
        const std::optional<menu::RaceRequest> race = enter_sunday_cup(f);
        CHECK(race && race->sequence == "solo_event" && race->race == "am_sunday_0001" && race->car == car && race->garage_slot == 0);
        if (!race) return;
        CHECK(menu::race_equipment(g, race->garage_slot).has_value());
        Sequence q(f, *race);
        q.fill();
        CHECK(q.menus({menu::Key::Accept}) == "COMMAND_START");
        money = g.money, today = g.today;
        CHECK(q.ran(q.result(0)) == 600 && q.work().get("prize") == 600 && q.work().get("playerGridNumber") == 0);
        CHECK(after_race(f, q));
        CHECK(g.money == money + 600 && g.today == today + 1 && g.diary.back().type == 3 && g.rank_counts[1] == 1);
        // Back in Gran Turismo mode.
        CHECK(q.leave() && f.rt->project()->name() == "gtmode");
        std::printf("  back at %s, %lld credits, day %u\n", f.page().c_str(), static_cast<long long>(g.money), g.today);
        CHECK(f.page() == "EventCourseRoot"); // the event's page, where the player left
        // Back on the map the game saves itself (the option "auto save" of a new game).
        CHECK(g.need_save && f.rt->game().option_int("auto_save") == 1);
        for (int i = 0; i < 4 && f.page() != "TopRoot"; ++i) f.press({menu::Key::Cancel});
        CHECK(f.page() == "TopRoot" && !g.need_save);
        money = g.money, today = g.today;
        CHECK(f.clean());
    }
    {
        // The next start loads the game as it was saved on the way back.
        Flow f(*disc, "US", dir);
        f.rt->boot();
        f.rt->start_project(f.rt->first_project());
        f.rt->settle();
        for (int i = 0; i < 6 && !f.rt->finished(); ++i) f.press({f.page() == "MovieRoot" ? menu::Key::Start : menu::Key::Accept});
        menu::GameState& g = f.rt->game();
        CHECK(f.rt->finished() && g.money == money && g.today == today && g.garage_count() == 1 && g.rank_counts[1] == 1);
        // A game saved in Gran Turismo mode starts there again (the boot project's endBoot).
        CHECK(f.rt->advance());
        f.rt->settle();
        CHECK(f.rt->project()->name() == "gtmode" && f.page() == "TopRoot");
        script::Vm& vm = f.rt->vm();
        const Value label[1] = {Value::string("am_sunday_0001")};
        CHECK(vm.attribute(vm.call(vm.attribute(vm.attribute(vm.lookup("game"), "race_record"), "getUnit"), label), "result").as_string() == "1");
        // The cup's second race.
        const std::optional<menu::RaceRequest> race = enter_sunday_cup(f, 1);
        CHECK(race && race->race == "am_sunday_0002" && race->car == car && race->garage_slot == 0);
        if (race) std::printf("  next: %s at %s\n", race->race.c_str(), race->course.c_str());
        CHECK(f.clean());
    }
    if (!std::getenv("GT4R_KEEP_TEST_FILES")) std::filesystem::remove_all(dir);
}

namespace {

// An event's race run through its sequence: the race's menus, Start, the made-up result, the
// prize page and the results, Exit, and Gran Turismo mode again. The prize paid.
s64 race_event(Flow& f, const menu::RaceRequest& race, int place) {
    Sequence q(f, race);
    q.fill();
    if (q.menus({menu::Key::Accept}) != "COMMAND_START") return -1;
    const s64 prize = q.ran(q.result(place));
    if (!after_race(f, q) || !q.leave()) return -1;
    return prize;
}

} // namespace

// The Sunday Cup's five races won: back on Gran Turismo mode's map the present page gives the
// cup's prize car, and the garage has it.
TEST(disc_gt_mode_loop_prize_car) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    menu::GameState& g = f.rt->game();
    for (int race = 0; race < 5; ++race) {
        for (int i = 0; i < 4 && f.page() != "TopRoot"; ++i) f.press({menu::Key::Cancel});
        const std::optional<menu::RaceRequest> r = enter_sunday_cup(f, race);
        CHECK(r && r->race == "am_sunday_000" + std::to_string(race + 1));
        if (!r) return;
        CHECK(race_event(f, *r, 0) > 0);
        std::printf("  %s won: %s\n", r->race.c_str(), pages(f).c_str());
    }
    // The last win's way back opens the present page: the car is the garage's.
    CHECK(f.page() == "PresentRoot" && g.garage_count() == 2);
    if (g.garage.size() >= 2) {
        const menu::DayEvent& got = g.diary.back();
        std::printf("  prize car %s (paint %d); prize cars counted %u; the day: kind %d, how %d\n", g.garage[1].car.c_str(), g.garage[1].paint, g.total_prize_cars, got.type, got.b2);
        CHECK(!g.garage[1].car.empty() && got.type == 1 && got.b2 == 4);
    }
    f.press({menu::Key::Accept});
    CHECK(f.page() == "EventCourseRoot" && g.garage_count() == 2);
    CHECK(f.clean());
}
