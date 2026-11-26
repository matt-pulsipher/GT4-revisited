// What the gallery against the original's pictures found in the menus' drawing state: only the top page's focus
// is lit (a page under a dialog shows its focus at rest), and a tool tip is the focused widget's own.
#include "menu_run_flow.h"

using namespace gt4r::menu;

TEST(disc_menu_focus_lit_on_the_top_page_only) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    std::function<void(const LiveWidget&, std::vector<const LiveWidget*>&)> with_bits = [&](const LiveWidget& w, std::vector<const LiveWidget*>& out) {
        if (w.state & kFocus) out.push_back(&w);
        for (const auto& c : w.children) with_bits(*c, out);
    };
    // The map with its focus lit; Cancel asks whether to quit Gran Turismo mode (a dialog page over it).
    LiveWidget* map = f.rt->current_page();
    CHECK(map && map->focus.lock());
    std::vector<const LiveWidget*> lit;
    with_bits(*map, lit);
    CHECK(!lit.empty());
    f.press({Key::Cancel});
    f.rt->settle();
    LiveWidget* top = f.rt->current_page();
    CHECK(top && top != map && f.rt->open_pages().size() >= 2);
    for (const auto& page : f.rt->open_pages()) {
        std::vector<const LiveWidget*> got;
        with_bits(*page, got);
        if (page.get() == top) CHECK(!got.empty());
        else CHECK(got.empty());
    }
    // Closed again: the map's focus is lit again.
    f.press({Key::Cancel});
    f.rt->settle();
    lit.clear();
    if (f.rt->current_page() == map) with_bits(*map, lit);
    CHECK(f.rt->current_page() == map && !lit.empty());
}
