// The options taking effect: the title's demo after the option's seconds without a key, the
// options given to the rest of the game (Runtime::on_options) when the scripts apply them.

#include "menu_run_flow.h"

#include "gt4r/menu/game_settings.h"

#include <cstdio>

using script::Value;

// The title waits timeout_count_to_autodemo seconds after the last key, then asks for the demo.
TEST(disc_menu_title_demo_after_option) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    for (const int seconds : {20, 5}) {
        Flow f(*disc);
        CHECK(f.boot_to_title());
        f.rt->game().options["timeout_count_to_autodemo"] = seconds;
        f.press({menu::Key::Down, menu::Key::Up});
        const u64 start = f.rt->frame();
        int frames = 0;
        for (; frames < 60 * 40 && !f.rt->finished(); ++frames) f.rt->update();
        const float waited = float(f.rt->frame() - start) / 60.0f;
        std::printf("  option %d s: the title waited %.2f s, next sequence %s\n", seconds, double(waited), f.rt->game().next_sequence.c_str());
        CHECK(f.rt->finished() && f.rt->game().next_sequence == "autodemo" && waited > float(seconds) && waited < float(seconds) + 1.5f);
    }
}

// The options' apply hands the settings to whoever reads them; a game loaded does too.
TEST(disc_menu_options_given_on_apply) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    int calls = 0;
    menu::GameSettings last;
    f.rt->on_options = [&](const menu::GameSettings& s) { ++calls, last = s; };
    CHECK(f.boot_to_title());
    CHECK(calls == 1 && last.menu.music > 0.8f);
    script::Vm& vm = f.rt->vm();
    const Value option = vm.attribute(vm.lookup("game"), "option");
    // The Picture Quality page's arrays and the volume as the option pages write them.
    auto set = [&](const char* name, const Value& v) { option.p->member(vm, option, name)->set(vm, v); };
    set("race_color_correction", Value::array({Value::integer(-1), Value::integer(2), Value::integer(0), Value::integer(5)}));
    set("menu_bgm_volume_raw", Value::integer(32));
    const Value back = vm.attribute(option, "race_color_correction");
    CHECK(back.items() && back.items()->size() == 4 && (*back.items())[3].i == 5);
    vm.call(vm.attribute(option, "apply"), {});
    CHECK(calls == 2 && last.menu.music == 0.25f && last.race_picture == (std::array<int, 4>{-1, 2, 0, 5}));
}
