// Pages of Gran Turismo mode against the reference, one check an item: the race tables' car names, the diary's colors, the
// question a won event's entry asks.

#include "menu_run_flow.h"

#include "gt4r/menu/game_internal.h"
#include "gt4r/menu/race_natives.h"

#include <cstdio>

using script::Value;

// The race menus' tables show the car's short name (0x445450: CAR_NAME +0x08), as the reference's grids
// (21-gtmode-deep: "Nissan SKYLINE GT-R V-spec II Nur `02", its full name has "(R34)").
TEST(disc_menu_race_table_short_names) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    f.rt->boot();
    menu::QuickWork& w = menu::quick_work(f.rt->game());
    w.rows.assign(2, menu::QuickRow{});
    w.rows[0].car = "r34_gtr_v2_nur_02", w.rows[1].car = "r34_gtr_rtune_99";
    script::Vm& vm = f.rt->vm();
    const Value cls = vm.lookup("menu::MQuickWork");
    const Value work = cls.p ? vm.call(cls, {}) : Value{};
    CHECK(work.p != nullptr);
    if (!work.p) return;
    const Value i0[1] = {Value::integer(0)}, i1[1] = {Value::integer(1)};
    CHECK(vm.call(vm.attribute(work, "getGridCarName"), i0).to_string() == "Nissan SKYLINE GT-R V-spec II Nur `02");
    CHECK(vm.call(vm.attribute(work, "getGridCarName"), i1).to_string() == "NISMO Skyline GT-R R-tune (R34) `99"); // (its short name has it)
}

// A text face's `color` takes a color object (the diary's days: 0.3 gray a car got, 0.25 0.25 0.35 a license), and the same
// back as the color it was given.
TEST(disc_menu_text_face_color_object) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    f.map_focus("Home");
    f.press({menu::Key::Accept});
    CHECK(f.focus_on("Log") || f.focus_on("Diary"));
    f.press({menu::Key::Accept});
    for (int i = 0; i < 120; ++i) f.rt->update();
    f.rt->settle();
    std::printf("  %s\n", f.page().c_str());
    int colored = 0;
    std::function<void(const std::shared_ptr<menu::LiveWidget>&)> walk = [&](const std::shared_ptr<menu::LiveWidget>& w) {
        if (w->name == "record" && w->is("TextFace") && !w->data.text("text").empty()) {
            const std::optional<menu::Rgba> c = w->data.color("text_color");
            std::printf("  \"%s\": %s\n", w->data.text("text").c_str(), c ? (std::to_string(c->r) + " " + std::to_string(c->g) + " " + std::to_string(c->b)).c_str() : "no color");
            colored += c && c->r == 77 && c->g == 77 && c->b == 77; // the car bought: 0.3
        }
        for (const auto& c : w->children) walk(c);
    };
    if (f.rt->current_page()) walk(f.rt->current_page()->self());
    CHECK(colored > 0);
}

// A single-race event all won asks before it is entered again (EventCourseRoot's ComfirmClearSingleRaceResult; the
// original on this card, reference 31-progress6): "Don't Clear" goes on to the race, the records kept.
TEST(disc_gt_mode_won_event_asks) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    if (f.page() != "TopRoot") return;
    menu::GameState& g = f.rt->game();
    menu::Database& d = menu::natives_of(*f.rt)->db();
    for (int k = 1; k <= 9; ++k) {
        const int unit = d.race_unit("am_sunday_000" + std::to_string(k));
        if (unit >= 0) g.race_units[size_t(unit)] = 2; // a race won, seen by the event's page (the words' bits 8 .. 15 "1")
    }
    const Value code[1] = {Value::string("am_sunday_0000")};
    CHECK(f.rt->vm().call(f.rt->vm().lookup("GTmodeProject::isSpotRaceCompleted"), code).truthy());
    f.map_focus("BeginnerHall");
    f.press({menu::Key::Accept});
    for (int i = 0; i < 90; ++i) f.rt->update();
    f.press({menu::Key::Accept});
    for (int i = 0; i < 90; ++i) f.rt->update();
    f.focus_on("OK");
    f.press({menu::Key::Accept});
    for (int n = 0; n < 6 && f.page() != "ConfirmDialog" && !f.rt->race_request(); ++n) {
        for (int i = 0; i < 120; ++i) f.rt->update();
        std::printf("  %s [%s]\n", f.page().c_str(), f.focus().c_str());
        f.press({menu::Key::Accept});
    }
    std::printf("  then %s [%s]\n", f.page().c_str(), f.focus().c_str());
    CHECK(f.page() == "ConfirmDialog" && !f.rt->race_request());
}
