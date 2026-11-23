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

// What the scripts asked of stand-ins (the program's objects not written), listed.
void print_stubs(Flow& f) {
    for (const auto& [what, n] : f.rt->vm().stub_uses) std::printf("  stand-in: %s (%u)\n", what.c_str(), n);
}

// A widget of the page shown by its name (depth first).
std::shared_ptr<menu::LiveWidget> widget(Flow& f, const std::string& name) {
    std::function<std::shared_ptr<menu::LiveWidget>(const std::shared_ptr<menu::LiveWidget>&)> find = [&](const std::shared_ptr<menu::LiveWidget>& w) {
        if (w->name == name) return w;
        for (const auto& c : w->children)
            if (auto found = find(c)) return found;
        return std::shared_ptr<menu::LiveWidget>();
    };
    return f.rt->current_page() ? find(f.rt->current_page()->self()) : nullptr;
}

// The prize page has counted its prize into the credits: its OK shows.
bool prize_counted(Flow& f) {
    const std::shared_ptr<menu::LiveWidget> ok = f.rt->current_page() ? f.rt->current_page()->child("OK") : nullptr;
    return ok && ok->visible() && f.focus() == "OK";
}

// The race's menus after a race: the prize page counts the prize into the credits, then the
// results page; Exit ends the sequence. True when it ended with Exit.
bool after_race(Flow& f, Sequence& q) {
    f.rt->start_project(q.project);
    f.rt->settle();
    if (f.page() == "PrizeRoot") {
        for (int i = 0; i < 8000 && !prize_counted(f); ++i) f.rt->update();
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
    print_stubs(f);
    CHECK(f.clean());
}

// A championship of five races (the Mazda Roadster cup at Mazda's): each race's points by
// place, its prize, Next to the following race; after the last the championship's grand prize
// and its record, and its prize car back in Gran Turismo mode.
TEST(disc_gt_mode_loop_championship) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    menu::GameState& g = f.rt->game();
    menu::RaceRequest request = g.race;
    request.sequence = "championship", request.race = "jp_mazda_roadster_0500", request.car = g.garage[0].car, request.garage_slot = 0;
    g.next_sequence = "championship", g.sequence_asked = true;
    f.rt->finish();
    Sequence q(f, request);
    const s64 money = g.money;
    s64 total = 0, grand = 0;
    for (int k = 1; k <= 5; ++k) {
        q.request.race = "jp_mazda_roadster_050" + std::to_string(k);
        q.fill();
        q.work().set("SessionNumber", k - 1), q.work().set("IsFinalSession", k == 5);
        CHECK(q.menus({menu::Key::Accept}) == "COMMAND_START");
        menu::RaceResult r = q.result(k == 2 ? 1 : 0);
        r.kind = menu::RaceResult::Kind::Championship, r.series = "jp_mazda_roadster_0500";
        const s64 paid = q.ran(r);
        // Points 10, 6, 4, 3, 2, 1 by place: the player second in the second race.
        CHECK(g.series.label == "jp_mazda_roadster_0500" && g.series.races_run == k && !g.series.points.empty());
        if (g.series.points.size() >= 2) CHECK(g.series.points[0] == (k >= 2 ? 10 * k - 4 : 10) && g.series.points[1] == (k >= 2 ? 6 * (k - 1) + 10 : 6));
        CHECK(paid > 0 && q.work().get("prize") > 0 && q.work().get("GrandPrize") == (k == 5 ? q.work().get("GrandPrize") : 0));
        if (k == 5) {
            grand = q.work().get("GrandPrize");
            CHECK(grand > 0 && q.work().get("SeriesRank") == 0);
        }
        total += paid;
        f.rt->start_project(q.project);
        f.rt->settle();
        for (int pages_shown = 0; pages_shown < 2 && f.page() == "PrizeRoot"; ++pages_shown) {
            for (int i = 0; i < 8000 && !prize_counted(f); ++i) f.rt->update();
            f.rt->settle();
            f.press({menu::Key::Accept});
        }
        CHECK(f.page() == "AfterRoot");
        // The results' title names the race by the text's marks ("{0} Race No. {1}").
        const std::string title = f.text_of("GridTitle_text");
        CHECK(!title.empty() && title.find('{') == std::string::npos);
        if (k == 1) std::printf("  results of %s\n", title.c_str());
        if (k < 5) {
            CHECK(f.focus_on("Next"));
            f.press({menu::Key::Accept});
            CHECK(f.rt->finished() && q.work().take_command() == "COMMAND_NEXT_RACE");
            q.work().set("IsSessionFinished", 0), q.work().set("canReplay", 0);
        } else {
            // After the last race the page offers no Next: leaving ends the championship.
            f.press({menu::Key::Cancel});
            CHECK(f.rt->finished() && q.work().take_command() == "COMMAND_EXIT");
        }
    }
    std::printf("  five races: %lld credits with a grand prize of %lld\n", static_cast<long long>(total), static_cast<long long>(grand));
    CHECK(g.money == money + total);
    // The championship's own record: won.
    script::Vm& vm = f.rt->vm();
    const Value label[1] = {Value::string("jp_mazda_roadster_0500")};
    CHECK(vm.attribute(vm.call(vm.attribute(vm.attribute(vm.lookup("game"), "race_record"), "getUnit"), label), "result").as_string() == "1");
    CHECK(q.leave() && f.page() == "PresentRoot" && g.garage_count() == 2);
    if (g.garage.size() >= 2) std::printf("  prize car %s\n", g.garage[1].car.c_str());
    print_stubs(f);
    CHECK(f.clean());
}

// The B license: its first test from the License Center through the test's own menus (passed
// with gold), the other fifteen passed; back in Gran Turismo mode the license is given (the
// present page's license paper), and the License Center shows it held.
TEST(disc_gt_mode_loop_license) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    menu::GameState& g = f.rt->game();
    script::Vm& vm = f.rt->vm();
    const Value record = vm.attribute(vm.lookup("game"), "race_record");
    const Value l0b[1] = {Value::string("l0b")};
    CHECK(vm.call(vm.attribute(record, "isLicensePassed"), l0b).i == 0);
    f.map_focus("License");
    f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
    const std::optional<menu::RaceRequest> race = f.rt->race_request();
    CHECK(race && race->sequence == "license" && race->race == "l0b0001");
    if (!race) return;
    Sequence q(f, *race);
    q.fill(1);
    CHECK(q.menus({menu::Key::Accept}) == "COMMAND_START");
    menu::RaceResult r;
    r.kind = menu::RaceResult::Kind::License, r.race = race->race, r.result = "gold", r.time_ms = 10'010;
    q.ran(r);
    f.rt->start_project(q.project);
    f.rt->settle();
    // After a run the test's page offers its replay and the next test.
    CHECK(f.page() == "LicenseRoot" && f.focus_on("Replay") && f.focus_on("Next"));
    // The other fifteen tests passed.
    for (int k = 2; k <= 16; ++k) {
        menu::RaceResult t = r;
        char label[16];
        std::snprintf(label, sizeof label, "l0b%04d", k);
        t.race = label, t.result = k % 2 ? "silver" : "bronze";
        f.rt->race_finished(t);
    }
    CHECK(vm.call(vm.attribute(record, "isLicensePassed"), l0b).i == 1);
    CHECK(f.focus_on("Exit"));
    f.press({menu::Key::Accept});
    CHECK(f.rt->finished() && q.work().take_command() == "COMMAND_EXIT" && q.leave());
    // The present page: first the license's paper with the player's name, then the license's car.
    CHECK(f.page() == "PresentRoot");
    for (int k = 0; k < 900; ++k) f.rt->update();
    f.rt->settle();
    const std::shared_ptr<menu::LiveWidget> pane = f.rt->current_page()->child("PaperPane");
    const std::shared_ptr<menu::LiveWidget> paper = pane ? pane->child("Paper") : nullptr;
    CHECK(paper && pane->shown() && !f.rt->current_page()->child("CarPane")->shown() && g.garage_count() == 1);
    if (paper) {
        const std::string name = paper->child("Name")->data.text("text"), license = paper->child("License")->data.text("text");
        std::printf("  the license paper: %s, %s\n", name.c_str(), license.c_str());
        CHECK(name == g.username && license == "B License" && paper->child("bg")->data.text("image_path").find("l0b") != std::string::npos);
    }
    f.press({menu::Key::Accept});
    for (int k = 0; k < 900; ++k) f.rt->update();
    f.rt->settle();
    CHECK(f.page() == "PresentRoot" && g.garage_count() == 2);
    if (g.garage.size() >= 2) std::printf("  the license's car: %s\n", g.garage[1].car.c_str());
    f.press({menu::Key::Accept});
    CHECK(f.page() == "LicenseDetailRoot");
    print_stubs(f);
    CHECK(f.clean());
}

// A driving mission from the map: its race menus, the mission won (first), its prize counted, its
// record by place, and the map again.
TEST(disc_gt_mode_loop_mission) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    menu::GameState& g = f.rt->game();
    f.map_focus("Mission");
    f.press({menu::Key::Accept, menu::Key::Accept});
    const std::optional<menu::RaceRequest> race = f.rt->race_request();
    CHECK(race && race->sequence == "mission");
    if (!race) return;
    Sequence q(f, *race);
    q.fill(2);
    const std::string command = q.menus({menu::Key::Accept});
    std::printf("  %s: %s at %s\n", race->race.c_str(), race->car.c_str(), race->course.c_str());
    CHECK(command == "COMMAND_START");
    menu::RaceResult r = q.result(0);
    r.kind = menu::RaceResult::Kind::Mission;
    const s64 money = g.money;
    const s64 prize = q.ran(r);
    CHECK(prize > 0 && q.work().get("prize") == prize && g.diary.back().type == 5);
    CHECK(after_race(f, q) && g.money == money + prize);
    script::Vm& vm = f.rt->vm();
    const Value record = vm.attribute(vm.lookup("game"), "race_record");
    const Value label[1] = {Value::string(race->race)};
    const Value unit = vm.call(vm.attribute(record, "getUnit"), label);
    CHECK(vm.attribute(unit, "type").as_string() == "mission" && vm.attribute(unit, "result").as_string() == "1");
    // The present rule's question: missions 1 to 1 cleared in first place.
    const Value clear[2] = {Value::string("1,1"), Value::string("1")};
    CHECK(race->race != "l0m0001" || vm.call(vm.attribute(record, "isMissionClear"), clear).i == 1);
    CHECK(q.leave() && f.rt->project()->name() == "gtmode");
    std::printf("  back at %s, prize %lld\n", f.page().c_str(), static_cast<long long>(prize));
    print_stubs(f);
    CHECK(f.clean());
}
