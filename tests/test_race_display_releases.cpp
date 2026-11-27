// The race display's tables in each release's program (race/race_display.h): Japan's single race has rows for the A-spec
// points' two lines, at the places the other releases' program puts them.

#include "test_framework.h"

#include "disc/core_image.h"
#include "gt4r/race/race_display.h"

using namespace gt4r;

TEST(disc_race_display_points_rows) {
    int checked = 0;
    for (const std::unique_ptr<Disc>& d : test::discs()) {
        if (d->info().game != Game::GT4) continue;
        const gt3r::CoreImage core = gt3r::CoreImage::decode(d->read_program());
        const race::DisplayLayout l = race::read_single_race_layout(core.program, core.load_address);
        CHECK(l.valid());
        const race::DisplayPlace* title = l.find(race::element::kPointsTitle);
        const race::DisplayPlace* line = l.find(race::element::kPointsLine);
        std::printf("  %s: %zu rows, the points' rows %s\n", d->info().product.c_str(), l.places.size(), title && line ? "there" : "none");
        if (d->info().region == "jp") CHECK(title && line && title->x == 320 && title->y == 264 && line->x == 320 && line->y == 304);
        else CHECK(!title && !line);
        ++checked;
    }
    if (!checked) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
}
