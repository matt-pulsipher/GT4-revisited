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
        // A car got in Gran Turismo mode is open in Arcade from now on.
        CHECK(g.available_cars.count(car) == 1);
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
        CHECK(g.available_cars.count(car) == 1);
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
    const bool had_prize_car = g.available_cars.count("mazda_suv_concept_05") > 0;
    for (int k = 1; k <= 5; ++k) {
        q.request.race = "jp_mazda_roadster_050" + std::to_string(k);
        q.fill();
        q.work().set("SessionNumber", k - 1), q.work().set("IsFinalSession", k == 5);
        CHECK(q.menus({menu::Key::Accept}) == "COMMAND_START");
        menu::RaceResult r = q.result(k == 2 ? 1 : 0);
        r.kind = menu::RaceResult::Kind::Championship, r.series = "jp_mazda_roadster_0500";
        const s64 paid = q.ran(r);
        // Points 10, 6, 4, 3, 2, 1 by place: the player second in the second race.
        CHECK(g.series.label == "jp_mazda_roadster_0500" && g.series.races_run == k && g.series.race_count == 5 && g.series.entrants.size() == 6);
        if (g.series.entrants.size() >= 2)
            CHECK(g.series.entrants[0].points == (k >= 2 ? 10 * k - 4 : 10) && g.series.entrants[1].points == (k >= 2 ? 6 * (k - 1) + 10 : 6));
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
        // Session Results' Pts and Total: the place's points and the points so far (0x11A168).
        if (k == 1) {
            const std::vector<menu::QuickRow>& rows = q.work().rows;
            CHECK(rows.size() == 6 && rows[0].weight == 10 && rows[0].power == 10 && rows[5].weight == 1);
        }
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
    if (g.garage.size() >= 2) {
        // The prize car is open in Arcade from now on.
        std::printf("  prize car %s (Arcade had it before: %d)\n", g.garage[1].car.c_str(), had_prize_car);
        CHECK(!had_prize_car && g.available_cars.count(g.garage[1].car) == 1);
    }
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
    // Test 8's and test 10's cones: the page reads the test's number from "l0b0008" as a decimal (the string's toInt,
    // atoi), the test's own car shown (playtest's B36: an octal reading showed test 8 of no car and test 10 as 8).
    for (const char* test : {"l0008", "l0010"}) {
        CHECK(f.focus_on(test));
        const std::shared_ptr<menu::LiveWidget> pane = f.rt->current_page()->child("Pane");
        const std::shared_ptr<menu::LiveWidget> name = pane ? pane->child("CarName") : nullptr;
        const std::string car = name ? name->data.text("text") : std::string();
        std::printf("  %s: %s\n", test, car.c_str());
        CHECK(!car.empty());
    }
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
    f.press({menu::Key::Accept});
    f.press_licensed(menu::Key::Accept); // (the first mission asks for the International B license)
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

// A championship left after its first race: the race's menus ask whether to leave for now, the
// championship is kept, Gran Turismo mode offers it again on the map.
TEST(disc_gt_mode_loop_championship_left) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    menu::GameState& g = f.rt->game();
    menu::RaceRequest request = g.race;
    request.sequence = "championship", request.race = "jp_mazda_roadster_0501", request.car = g.garage[0].car, request.garage_slot = 0;
    g.major_menu_project = "gtmode", g.major_menu_page = "";
    // As the event page does before a championship (saveMenuStack), then the sequence.
    script::Vm& vm = f.rt->vm();
    vm.call(vm.attribute(vm.lookup("game"), "saveMenuStack"), {});
    g.next_sequence = "championship", g.sequence_asked = true;
    f.rt->finish();
    Sequence q(f, request);
    q.fill();
    q.work().set("SessionNumber", 0), q.work().set("IsFinalSession", 0);
    CHECK(q.menus({menu::Key::Accept}) == "COMMAND_START");
    menu::RaceResult r = q.result(0);
    r.kind = menu::RaceResult::Kind::Championship, r.series = "jp_mazda_roadster_0500";
    q.ran(r);
    f.rt->start_project(q.project);
    f.rt->settle();
    for (int i = 0; i < 8000 && !prize_counted(f); ++i) f.rt->update();
    f.rt->settle();
    f.press({menu::Key::Accept});
    CHECK(f.page() == "AfterRoot");
    // Leaving before the last race: "leave for now?", yes; the sequence keeps the championship.
    f.press({menu::Key::Cancel});
    CHECK(f.page() == "ConfirmDialog" && f.focus() == "Yes");
    f.press({menu::Key::Accept});
    CHECK(f.rt->finished() && q.work().take_command() == "COMMAND_EXIT");
    g.suspend_series(true);
    CHECK(q.leave() && f.page() == "TopRoot" && g.suspended == 1 && g.suspended_racecode == "jp_mazda_roadster_0500" && g.suspended_session == 1);
    // The map says a championship is under way; an event hall offers to go on with it.
    const std::shared_ptr<menu::LiveWidget> info = widget(f, "Info");
    CHECK(info && info->shown());
    f.map_focus("BeginnerHall");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "ConfirmDialog" && f.text_of("message").find("Roadster") != std::string::npos);
    std::printf("  %s\n", f.text_of("message").c_str());
    f.press({menu::Key::Accept});
    for (int i = 0; i < 4 && !f.rt->finished() && f.page() == "ConfirmDialog" && f.focus() != "Yes"; ++i) f.press({menu::Key::Right});
    f.press({menu::Key::Accept});
    const std::optional<menu::RaceRequest> again = f.rt->race_request();
    CHECK(again && again->sequence == "championship" && again->race == "jp_mazda_roadster_0500" && g.series.races_run == 1);
    if (again) std::printf("  asked again: %s %s, %d of %d races run\n", again->sequence.c_str(), again->race.c_str(), g.series.races_run, g.series.race_count);
    print_stubs(f);
    CHECK(f.clean());
}

// The garage's change of the car driven: to a car never driven its record is made as sold and
// cancelRidingCar takes the change back; a car already driven keeps its record and a cancel
// leaves it (0x435388, 0x435418, 0x435568).
TEST(disc_gt_mode_riding_car) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    menu::GameState& g = f.rt->game();
    script::Vm& vm = f.rt->vm();
    const Value garage = vm.attribute(vm.lookup("game"), "garage");
    const Value add[2] = {Value::string("demio_glx_99"), Value::integer(0)};
    const s32 place = vm.call(vm.attribute(garage, "addNewCar"), add).i;
    CHECK(place == 1 && !g.garage[1].record && g.garage[1].ride_rank == 0x3FF);
    const Value before = vm.attribute(garage, "riding_car");
    const Value change[2] = {Value::integer(1), Value{}};
    CHECK(vm.call(vm.attribute(garage, "changeRidingCar"), change).as_string() == "OK");
    CHECK(g.riding_car == 1 && g.riding_fresh && g.garage[1].record && g.garage[1].ride_rank == 0 && g.garage[0].ride_rank == 1);
    // Taken back: the Demio has no record again, the first car is driven.
    const Value back[2] = {Value::integer(0), before};
    vm.call(vm.attribute(garage, "cancelRidingCar"), back);
    CHECK(g.riding_car == 0 && !g.riding_fresh && !g.garage[1].record && g.garage[0].ride_rank == 0 && g.garage[0].record);
    // A change to it again, kept; then to the first car (driven before): no cancel undoes that.
    vm.call(vm.attribute(garage, "changeRidingCar"), change);
    const Value first[2] = {Value::integer(0), Value{}};
    vm.call(vm.attribute(garage, "changeRidingCar"), first);
    CHECK(g.riding_car == 0 && !g.riding_fresh && g.garage[1].record && g.garage[1].ride_rank == 1);
    const Value to_demio[2] = {Value::integer(1), Value{}};
    vm.call(vm.attribute(garage, "cancelRidingCar"), to_demio);
    CHECK(g.riding_car == 0 && g.garage[1].record);
    // The garage's lists by country: both countries in the order of their numbers, a country's cars.
    const Value countries = vm.call(vm.attribute(garage, "pickupCountry"), {});
    CHECK(countries.items() && countries.items()->size() == 2);
    if (countries.items() && countries.items()->size() == 2) {
        std::printf("  the garage's countries: %s, %s\n", (*countries.items())[0].to_string().c_str(), (*countries.items())[1].to_string().c_str());
        const Value jp[3] = {(*countries.items())[0], Value::string("all"), Value::string("all")};
        const Value cars = vm.call(vm.attribute(garage, "pickup"), jp);
        CHECK(cars.items() && cars.items()->size() == 1);
    }
    const Value all[3] = {Value::string("all"), Value::string("all"), Value::string("all")};
    const Value cars = vm.call(vm.attribute(garage, "pickup"), all);
    CHECK(cars.items() && cars.items()->size() == 2 && f.clean());
}

// Arcade's two-player battle: a course, the first player's car and paint, the second's (another
// car of the maker), Race Start: the race asked for has both cars.
TEST(disc_arcade_split_battle) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    f.press({menu::Key::Accept, menu::Key::Down});
    CHECK(f.focus() == "SplitBattle");
    f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
    CHECK(f.page() == "SplitRoot" && f.focus() == "Select");
    // Each player in turn: a maker, a car, its paint, the driving options.
    for (int player = 0; player < 2; ++player) {
        f.press({menu::Key::Accept});
        if (f.page() == "MakerRoot" && f.focus() == "Maker") f.press({menu::Key::Accept});
        f.press({menu::Key::Accept});
        CHECK(f.page() == "CarRoot");
        if (player == 1) f.press({menu::Key::Right});
        f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        CHECK(f.page() == "SplitRoot");
    }
    CHECK(f.focus() == "RaceStart");
    f.press({menu::Key::Accept});
    const std::optional<menu::RaceRequest> r = f.rt->race_request();
    CHECK(r && r->sequence == "split_battle" && !r->car.empty() && !r->car_2p.empty() && r->car != r->car_2p && !r->course.empty());
    if (r) std::printf("  split battle: %s and %s at %s\n", r->car.c_str(), r->car_2p.c_str(), r->course.c_str());
    CHECK(f.clean());
}

// Arcade's LAN battle with no network: the game says the Network Adaptor is needed, and stays.
TEST(disc_arcade_lan_battle_without_network) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.boot_to_title());
    f.press({menu::Key::Accept});
    CHECK(f.page() == "GameRoot" && f.focus_on("LANBattle"));
    f.press({menu::Key::Accept});
    CHECK(f.page() == "ConfirmDialog" && f.text_of("message").find("Network Adaptor") != std::string::npos);
    f.press({menu::Key::Accept});
    CHECK(f.page() == "GameRoot" && f.focus() == "LANBattle" && !f.rt->finished() && f.clean());
}

// The races' play list: the configuration fills it at power on (the US disc's 36 pieces of the
// racing group's 65 in their order, as the original's saved game has them), it is the game
// state's for the race's music, and a save keeps it.
TEST(disc_menu_race_playlist) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    f.rt->boot();
    const menu::GameState::PlayList& list = f.rt->game().bgm_playlist;
    int enabled = 0;
    for (u32 i = 0; i < list.rows; ++i) enabled += list.row[i].enabled;
    std::printf("  play list: %u rows, %d enabled, row 15 place %d, row 16 %d, row 17 %d\n", list.rows, enabled, list.row[15].order, list.row[16].order, list.row[17].order);
    CHECK(list.rows == 65 && enabled == 36 && !list.row[14].enabled && list.row[15].order == 0 && list.row[16].order == 1 && list.row[17].order == 3 && !list.none);
    CHECK(f.rt->game().slide_playlist.rows == 65);
}

// A license test's page visited and left without a run is a day gone by, with nothing in the
// diary (the sequence's end, 0x110800; the original's calendar moves a day for each visit); a run
// passed is that day's license run.
TEST(disc_gt_mode_license_visit_is_a_day) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    menu::GameState& g = f.rt->game();
    for (int visit = 0; visit < 2; ++visit) {
        for (int i = 0; i < 6 && f.page() != "TopRoot"; ++i) f.press({menu::Key::Cancel});
        f.map_focus("License");
        f.press({menu::Key::Accept, menu::Key::Accept, menu::Key::Accept});
        const std::optional<menu::RaceRequest> race = f.rt->race_request();
        CHECK(race && race->sequence == "license");
        if (!race) return;
        const u32 today = g.today;
        const size_t days = g.diary.size();
        Sequence q(f, *race);
        q.fill(1);
        f.rt->game().during_quick_menu = true;
        f.rt->start_project(q.project);
        f.rt->settle();
        if (visit == 1) {
            menu::RaceResult r;
            r.kind = menu::RaceResult::Kind::License, r.race = race->race, r.result = "bronze", r.time_ms = 11'166;
            q.ran(r);
            CHECK(g.today == today); // not until the sequence ends
            f.rt->start_project(q.project);
            f.rt->settle();
        }
        CHECK(f.focus_on("Exit"));
        f.press({menu::Key::Accept});
        CHECK(f.rt->finished() && q.work().take_command() == "COMMAND_EXIT" && q.leave());
        std::printf("  visit %d: day %u -> %u, the diary's last kind %d\n", visit, today, g.today, g.diary.empty() ? -1 : g.diary.back().type);
        CHECK(g.today == today + 1 && g.diary.size() == days + 1);
        CHECK(g.diary.back().type == (visit == 0 ? 0 : 4));
        if (visit == 1) CHECK(g.diary.back().b1 == 11 && g.diary.back().word == 11'166);
    }
    CHECK(f.clean());
}

// The gears' settings page's ranges (getGearRange, 0x3553A0): each gear's ends about the ratio
// the slider and final drive give, a third of the way to its neighbours.
TEST(disc_gt_mode_gear_ranges) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    script::Vm& vm = f.rt->vm();
    const Value car = vm.attribute(vm.attribute(vm.lookup("game"), "garage"), "riding_car");
    const Value zero[1] = {Value::integer(0)};
    const Value range = vm.call(vm.attribute(car, "getGearRange"), zero);
    const std::vector<Value>* v = range.items();
    CHECK(v && v->size() == 23);
    if (!v || v->size() < 23) return;
    const int n = int((*v)[4].i);
    std::printf("  %d gears:", n);
    for (int g = 1; g <= n; ++g) {
        const Value gi[1] = {Value::integer(g)};
        const s32 ratio = s32(vm.call(vm.attribute(car, "getGearSetting"), gi).i);
        const s32 lo = s32((*v)[size_t(7 + 2 * g)].i), hi = s32((*v)[size_t(8 + 2 * g)].i);
        std::printf(" %d<%d<%d", lo, ratio, hi);
        CHECK(lo > 0 && lo < hi);
        if (g > 1) CHECK(hi < s32((*v)[size_t(8 + 2 * (g - 1))].i));
    }
    std::printf("\n");
    CHECK(n >= 4);
}

// Photo travel: the map's place, its first location's ticket, the photo sequence asked for with
// the car driven at the location's course.
TEST(disc_gt_mode_photo_travel) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    f.map_focus("PhotoTravel");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "PhotoTravelRoot");
    for (int i = 0; i < 60; ++i) f.rt->update();
    f.press({menu::Key::Accept});
    const std::optional<menu::RaceRequest> r = f.rt->race_request();
    CHECK(r && r->sequence == "photo_shoot" && !r->course.empty() && r->car == f.rt->game().garage[0].car);
    if (r) std::printf("  photo travel asks for %s at %s with %s\n", r->sequence.c_str(), r->course.c_str(), r->car.c_str());
    CHECK(f.clean());
}
