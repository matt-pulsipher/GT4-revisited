// Trading cars (Home's Trade): the game on the card in slot 2 is loaded beside the one played (loadInstance), its
// garage listed in the Garage page's trade mode, and a car bought from it at its used price.

#include "menu_run_flow.h"

#include "gt4r/menu/game_progress.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <functional>

TEST(disc_gt_mode_trade_cars) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "gt4r-menu-test-trade";
    std::filesystem::remove_all(dir);
    std::filesystem::create_directories(dir);
    int other_cars = 0;
    {
        // The other game: played forward, saved, its card moved to slot 2.
        Flow f(*disc, "US", dir.string());
        menu::ProgressPlan plan = menu::default_progress_plan(*f.rt);
        plan.licenses.clear();
        const menu::ProgressReport r = menu::play_progress(*f.rt, plan);
        if (!r.ok) {
            std::printf("  the other game: %s\n", r.error.c_str());
            for (const std::string& step : r.steps) std::printf("    %s\n", step.c_str());
        }
        CHECK(r.ok);
        other_cars = f.rt->game().garage_count();
    }
    std::filesystem::rename(dir / "memorycard", dir / "memorycard2");
    Flow f(*disc, "US", dir.string());
    CHECK(f.gt_mode_with_a_car(500000));
    if (f.page() != "TopRoot") return;
    const menu::GameState& g = f.rt->game();
    const int mine = g.garage_count();
    const s64 money = g.money;
    f.map_focus("Home");
    f.press({menu::Key::Accept});
    std::printf("  home: %s [%s]\n", f.page().c_str(), f.focus().c_str());
    CHECK(f.focus_on("Trade"));
    f.press({menu::Key::Accept});
    std::printf("  trade: %s [%s]\n", f.page().c_str(), f.focus().c_str());
    for (int k = 0; k < 4 && f.page() == "ConfirmDialog"; ++k) {
        if (f.focus() != "Yes" && f.focus() != "OK") f.press({menu::Key::Left});
        f.press({menu::Key::Accept});
        std::printf("  then: %s [%s]\n", f.page().c_str(), f.focus().c_str());
    }
    CHECK(f.page() == "GarageRoot");
    if (std::getenv("GT4R_TRADE_TREE") && f.rt->current_page()) {
        std::function<void(const menu::LiveWidget&, int)> dump = [&](const menu::LiveWidget& w, int d) {
            if (d > 5) return;
            std::printf("%*s%s %s%s\n", d * 2, "", w.name.c_str(), w.data.text("text").c_str(), w.visible() ? "" : " (hidden)");
            for (const auto& c : w.children) dump(*c, d + 1);
        };
        dump(*f.rt->current_page(), 0);
    }
    // The list: the other game's cars with their trade prices.
    CHECK(f.focus_on("Line0"));
    const std::string name = f.text_of("name"), price = f.text_of("item0");
    std::printf("  the other garage's first car: %s, %s\n", name.c_str(), price.c_str());
    CHECK(!name.empty() && !price.empty());
    // A car bought from it: its used price paid, in the garage now.
    f.press({menu::Key::Accept});
    for (int k = 0; k < 4 && f.page() == "ConfirmDialog"; ++k) {
        std::printf("  dialog: %s [%s]\n", f.text_of("message").c_str(), f.focus().c_str());
        if (f.focus() == "No") f.press({menu::Key::Left});
        f.press({menu::Key::Accept});
    }
    std::printf("  garage %d -> %d, credits %lld -> %lld (other %d)\n", mine, g.garage_count(), (long long)money, (long long)g.money, other_cars);
    CHECK(g.garage_count() == mine + 1 && g.money < money);
    CHECK(f.clean());
}
