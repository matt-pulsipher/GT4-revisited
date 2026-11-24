// A game played forward through the menus without its races (menu/game_progress.h): cars bought,
// every license passed, the presents given, saved; loaded again by the next start, and put on a
// card image and taken back off it file for file.

#include "menu_run_flow.h"

#include "gt4r/menu/game_progress.h"
#include "gt4r/save/card_image.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

using script::Value;

namespace {

Bytes file_bytes(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    return Bytes((std::istreambuf_iterator<char>(in)), {});
}

} // namespace

TEST(disc_gt_mode_progress_saved_and_exported) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-menu-test-progress";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    int cars = 0;
    {
        Flow f(*disc, "US", dir.string());
        menu::ProgressPlan plan = menu::default_progress_plan(*f.rt);
        plan.credits = 1'234'567;
        const menu::ProgressReport r = menu::play_progress(*f.rt, plan);
        for (const std::string& line : r.steps) std::printf("  %s\n", line.c_str());
        if (!r.ok) std::printf("  error: %s\n", r.error.c_str());
        CHECK(r.ok);
        const menu::GameState& g = f.rt->game();
        cars = g.garage_count();
        // The bought cars and the licenses' prize cars; the last bought is driven.
        CHECK(cars > int(plan.cars.size()) && g.riding_car == int(plan.cars.size()) - 1 && g.money == 1'234'567);
        CHECK(f.clean());
    }
    {
        // The next start loads it and goes to Gran Turismo mode's map.
        Flow f(*disc, "US", dir.string());
        f.rt->boot();
        f.rt->start_project(f.rt->first_project());
        f.rt->settle();
        for (int i = 0; i < 6 && !f.rt->finished(); ++i) f.press({f.page() == "MovieRoot" ? menu::Key::Start : menu::Key::Accept});
        const menu::GameState& g = f.rt->game();
        CHECK(f.rt->finished() && f.rt->wanted_project() == "gtmode" && g.money == 1'234'567 && g.garage_count() == cars && g.username == "GT4R");
        script::Vm& vm = f.rt->vm();
        const Value record = vm.attribute(vm.lookup("game"), "race_record");
        for (const char* cat : {"l0b", "l0a", "lib", "lia", "l0s"}) {
            const Value a[1] = {Value::string(cat)};
            CHECK(vm.call(vm.attribute(record, "isLicensePerfect"), a).i == 1);
        }
    }
    // On a card image and back: the same four files.
    const std::filesystem::path card = dir / "memorycard";
    std::string why;
    const Bytes image = save::export_card(card.string(), std::nullopt, &why);
    CHECK(!image.empty());
    const std::filesystem::path back = dir / "back";
    std::filesystem::create_directories(back);
    CHECK(save::import_card(image, back.string()).ok);
    for (const char* name : {"BASCUS-97328GAMEDATA", "garage", "icon.sys", "icon0.ico"}) {
        const Bytes a = file_bytes(card / "BASCUS-97328GAMEDATA" / name), b = file_bytes(back / "BASCUS-97328GAMEDATA" / name);
        CHECK(!a.empty() && a == b);
    }
}
