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
