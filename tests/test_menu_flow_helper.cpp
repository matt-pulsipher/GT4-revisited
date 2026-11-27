// The tests' way into Gran Turismo mode with a car (menu_run_flow.h Flow::gt_mode_with_a_car) on every Gran Turismo 4 disc.

#include "menu_run_flow.h"

#include <cstdio>

TEST(disc_gt_mode_with_a_car_every_disc) {
    int discs = 0;
    for (const auto& d : test::discs()) {
        if (d->info().game != Game::GT4) continue;
        ++discs;
        Flow f(*d);
        const bool ok = f.gt_mode_with_a_car();
        std::printf("  %s (%s, language %s): %s, page %s, focus %s, %d cars, riding %d\n", d->info().product.c_str(), d->info().region.c_str(), d->info().language.c_str(),
                    ok ? "ok" : "FAILED", f.page().c_str(), f.focus().c_str(), f.rt->game().garage_count(), f.rt->game().riding_car);
        for (const std::string& line : f.rt->log())
            if (!ok && (line.find("error") != std::string::npos || line.find("page") == 0)) std::printf("    %s\n", line.c_str());
        CHECK(ok);
        CHECK(f.clean());
    }
    if (!discs) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
}
