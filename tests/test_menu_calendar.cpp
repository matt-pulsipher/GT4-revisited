// The options' copies (MGame.getOption / setOption), the course visits' diary day, the car's dirt,
// the title's demonstrations and the projects that go back to the last one: the game's rules as
// the program keeps them (docs/RULES_AUDIT.md items 4, 5, 12, 14, 20).

#include "menu_run_flow.h"

#include "gt4r/menu/game_settings.h"

#include <cstdio>

using script::Value;

namespace {

Disc* prologue_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::Prologue) return d.get();
    return nullptr;
}

Value call(script::Vm& vm, const Value& self, const char* name, std::vector<Value> args = {}) { return vm.call(vm.attribute(self, name), args); }

} // namespace

// getOption (0x161B88) gives a copy of the game's options: what is written into it, and its
// apply, stay the copy's (the preview: the settings go to whoever reads them) until setOption
// (0x161C38) makes it the game's and applies it. The game's own (`option`) stays live.
TEST(disc_menu_option_copy_and_commit) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    int calls = 0;
    menu::GameSettings last;
    f.rt->on_options = [&](const menu::GameSettings& s) { ++calls, last = s; };
    CHECK(f.boot_to_title());
    menu::GameState& g = f.rt->game();
    script::Vm& vm = f.rt->vm();
    const Value game = vm.lookup("game");
    g.options["wide_mode"] = 0, g.options["menu_bgm_volume_raw"] = 128;
    const Value copy = call(vm, game, "getOption");
    auto set = [&](const Value& o, const char* name, const Value& v) { o.p->member(vm, o, name)->set(vm, v); };
    set(copy, "wide_mode", Value::integer(1));
    set(copy, "menu_bgm_volume_raw", Value::integer(64));
    CHECK(vm.attribute(copy, "wide_mode").i == 1 && g.option_int("wide_mode") == 0);
    // The copy applied: the menus' music at the copy's level, the game's options as they were.
    const int before = calls;
    call(vm, copy, "apply");
    CHECK(calls == before + 1 && last.menu.music == 0.5f && g.option_int("menu_bgm_volume_raw") == 128);
    // Cancel's getOption: the game's values again.
    const Value again = call(vm, game, "getOption");
    CHECK(vm.attribute(again, "wide_mode").i == 0 && vm.attribute(again, "menu_bgm_volume_raw").i == 128);
    // setOption: the copy is the game's, applied.
    call(vm, game, "setOption", {copy});
    std::printf("  after setOption: wide_mode %d, music raw %d, last music %.2f\n", g.option_int("wide_mode"), g.option_int("menu_bgm_volume_raw"), double(last.menu.music));
    CHECK(g.option_int("wide_mode") == 1 && g.option_int("menu_bgm_volume_raw") == 64 && calls == before + 2 && last.menu.music == 0.5f);
    CHECK(vm.attribute(vm.attribute(game, "option"), "wide_mode").i == 1);
    // A copy made before the change no longer changes anything; the game's own is written at once.
    set(again, "wide_mode", Value::integer(0));
    CHECK(g.option_int("wide_mode") == 1);
    set(vm.attribute(game, "option"), "wide_mode", Value::integer(0));
    CHECK(g.option_int("wide_mode") == 0 && f.clean());
}

// Prologue's options (project option2): the Screen pane's slider previews on a copy (applyOption: getOption, the
// sliders' values, apply), Cancel shows the game's values again; Confirm makes them the game's (setOption).
TEST(disc_prologue_option_screen_cancel) {
    Disc* disc = prologue_disc();
    if (!disc) SKIP("set GT4R_DISC to the Prologue disc");
    Flow f(*disc, "JP");
    f.rt->boot();
    menu::GameState& g = f.rt->game();
    g.options["screen_adjust_h"] = 0, g.options["wide_mode"] = 0;
    f.rt->start_project("option2");
    f.rt->settle();
    CHECK(f.page() == "OptionRoot" && f.focus() == "Screen");
    f.press({menu::Key::Accept, menu::Key::Down}); // the Screen pane, its horizontal position
    CHECK(f.focus() == "pinch" && f.rt->focused()->parent_widget());
    if (f.focus() != "pinch" || !f.rt->focused()->parent_widget()) return;
    menu::LiveWidget* slider = f.rt->focused()->parent_widget();
    const float h0 = slider->data.number("index");
    f.press({menu::Key::Right, menu::Key::Right});
    const float moved = slider->data.number("index");
    std::printf("  %s: index %g -> %g, the game's screen_adjust_h %d\n", slider->name.c_str(), double(h0), double(moved), g.option_int("screen_adjust_h"));
    CHECK(moved == h0 + 2 && g.option_int("screen_adjust_h") == 0); // the preview is the copy's
    f.press({menu::Key::Cancel});
    std::printf("  after Cancel: focus %s, index %g, the game's %d\n", f.focus().c_str(), double(slider->data.number("index")), g.option_int("screen_adjust_h"));
    CHECK(f.focus() == "SelectBar" && slider->data.number("index") == h0 && g.option_int("screen_adjust_h") == 0);
    // Again, and Confirm: the game's.
    f.press({menu::Key::Accept, menu::Key::Down, menu::Key::Right});
    CHECK(slider->data.number("index") == h0 + 1);
    f.press({menu::Key::Down, menu::Key::Down}); // the vertical slider, the Decision row
    std::printf("  focus %s\n", f.focus().c_str());
    CHECK(f.focus_on("Confirm"));
    f.press({menu::Key::Accept});
    std::printf("  after Confirm: focus %s, the game's %d\n", f.focus().c_str(), g.option_int("screen_adjust_h"));
    CHECK(g.option_int("screen_adjust_h") == int(h0) + 1 && f.clean());
}

// A course visit is one day of the calendar, put when its sequence ends, whatever its runs (0x1179F8, 0x1184DC, 0x11B0D8,
// 0x115E90: RUN_COURSE, place 15, note 14): none for a run, one for a visit without a run. The free run's carries the
// session's best and note 13 when that best is the course's record.
TEST(disc_gt_mode_course_visit_is_a_day) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    menu::GameState& g = f.rt->game();
    f.map_focus("RealCircuit");
    f.press({menu::Key::Accept});
    for (int n = 0; n < 6 && !f.rt->race_request(); ++n) {
        for (int i = 0; i < 120; ++i) f.rt->update();
        f.press({menu::Key::Accept});
    }
    const std::optional<menu::RaceRequest> race = f.rt->race_request();
    CHECK(race.has_value());
    if (!race) return;
    std::printf("  asked: %s at %s\n", race->sequence.c_str(), race->course.c_str());
    CHECK(race->sequence == "freerun");
    const u32 today = g.today;
    const size_t days = g.diary.size();
    CHECK(g.race_session.sequence == "freerun" && g.race_session.course_kind == 1);
    Sequence q(f, *race);
    for (const u32 lap : {65'432u, 64'321u}) {
        menu::RaceResult r;
        r.kind = menu::RaceResult::Kind::Course, r.course_kind = 1, r.course = race->course, r.finished = true, r.time_ms = lap;
        q.ran(r);
        menu::QuickWork& w = q.work(); // (RaceScreen keeps the session's best)
        if (w.get("BestTime") == s32(menu::QuickWork::kNoTime) || s32(lap) < w.get("BestTime")) w.set("BestTime", s32(lap));
    }
    CHECK(g.today == today && g.diary.size() == days); // not by the run
    CHECK(q.leave());
    std::printf("  after the visit: day %u -> %u, the diary's last: kind %d, course kind %d, place %d, note %d, time %u\n", today, g.today,
                g.diary.back().type, g.diary.back().b3, g.diary.back().b1, g.diary.back().b2, g.diary.back().word);
    CHECK(g.today == today + 1 && g.diary.size() == days + 1);
    const menu::DayEvent& d = g.diary.back();
    CHECK(g.race_session.course_kind == 0 && g.course_unit(d.code, false) >= 0); // (the course's unit made, 0x433C10)
    const int unit = g.course_unit(d.code, false);
    auto entry = [&](size_t k) {
        u32 v = 0;
        for (int b = 0; b < 4; ++b) v |= u32(g.course_record[size_t(unit) * menu::kCourseUnit + 8 + k * 0x30 + size_t(b)]) << (8 * b);
        return v;
    };
    const bool record = unit >= 0 && entry(0) == 64'321 && entry(1) != 64'321; // (when the free run writes its ranking)
    CHECK(d.type == 8 && d.b3 == 1 && d.b1 == 15 && d.b2 == (record ? 13 : 14) && d.word == 64'321);
    if (unit < 0) return;
    // The session's best as the course's record: note 13; no best: no time, note 14.
    auto put = [&](size_t o, u32 v) { for (int k = 0; k < 4; ++k) g.course_record[size_t(unit) * menu::kCourseUnit + o + size_t(k)] = u8(v >> (8 * k)); };
    for (const bool record : {true, false}) {
        g.race_session = {}, g.race_session.sequence = "freerun", g.race_session.course_kind = 1, g.race_session.course_code = d.code;
        q.work().set("BestTime", record ? 64'321 : s32(menu::QuickWork::kNoTime));
        put(8, 64'321), put(8 + 0x30, menu::kNoTime);
        g.end_race_sequence();
        CHECK(g.diary.back().b2 == (record ? 13 : 14) && g.diary.back().word == (record ? 64'321u : menu::kNoTime));
    }
    // Practice, the photo drive, the machine test: the day as it is, the course kind theirs.
    for (const auto& [sequence, kind] : {std::pair<const char*, int>{"practice", 3}, {"photo_drive", 4}, {"machinetest", 6}}) {
        menu::RaceRequest r = *race;
        r.sequence = sequence;
        g.race_session = {}, g.race_session.sequence = sequence;
        menu::start_quick_work(*f.rt, r);
        menu::RaceResult run;
        run.kind = menu::RaceResult::Kind::Course, run.course_kind = kind, run.course = race->course, run.finished = true, run.time_ms = 70'000;
        const size_t before = g.diary.size();
        f.rt->race_finished(run);
        CHECK(g.diary.size() == before);
        g.end_race_sequence();
        const menu::DayEvent& e = g.diary.back();
        CHECK(g.diary.size() == before + 1 && e.type == 8 && e.b3 == kind && e.b1 == 15 && e.b2 == 14 && e.word == menu::kNoTime && e.code == d.code);
    }
    CHECK(f.clean());
}
