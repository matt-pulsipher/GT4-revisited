// The menus' drawing, round six, on the running pages: the missions' license badges and the scroll bar's thumb, the
// used cars' thumb and paint chips (reference 21-gtmode-deep/driving_missions_page, 18-round4-gt/used_car_showroom_2).

#include "menu_run_flow.h"

#include "gt4r/menu/runtime_internal.h"

#include <cmath>
#include <functional>

namespace {

// The first widget under `w` with that name.
menu::LiveWidget* named(menu::LiveWidget& w, const std::string& name) {
    for (const auto& c : w.children) {
        if (c->name == name) return c.get();
        if (menu::LiveWidget* found = named(*c, name)) return found;
    }
    return nullptr;
}

} // namespace

TEST(disc_menu_mission_badges_and_thumb) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    f.map_focus("Mission");
    f.press({menu::Key::Accept});
    CHECK(f.page() == "MissionRoot");
    if (f.page() != "MissionRoot") return;
    for (int i = 0; i < 30; ++i) f.rt->update();
    menu::LiveWidget& page = *f.rt->current_page();
    menu::LiveWidget* window = named(page, "ScrollWindow");
    CHECK(window != nullptr);
    if (!window) return;
    menu::LiveWidget* space = named(*window, "space");
    menu::LiveWidget* bar = named(*window, "hbar");
    CHECK(space && bar && !space->children.empty());
    if (!space || !bar || space->children.empty()) return;
    // The first mission asks for the International B license (index 3 of the script's table).
    menu::LiveWidget* badge = named(*space->children[0], "license");
    CHECK(badge && badge->visible() && badge->data.text("image_path") == "image/gtmode/mission/lib.png");
    // 34 missions, two a column: the base the whole bar, the thumb from its start, the window's part of the space.
    const std::optional<menu::Rect> base = named(*bar, "base")->data.rect("geometry"), pinch = named(*bar, "pinch")->data.rect("geometry"),
                                    whole = space->data.rect("geometry");
    CHECK(base && pinch && whole);
    if (!base || !pinch || !whole) return;
    CHECK(base->x == 0 && base->w == 640);
    CHECK(std::fabs(pinch->x) < 1 && std::fabs(pinch->w - 640.0f * 640.0f / whole->w) < 0.5f && pinch->w > 20 && pinch->w < 80);
    CHECK(f.clean());
}

TEST(disc_menu_used_car_thumb_and_chips) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    Flow f(*disc);
    CHECK(f.gt_mode_with_a_car());
    f.map_focus("UsedCarEU");
    f.press({menu::Key::Accept});
    for (int i = 0; i < 4 && f.page() != "UsedCarRoot"; ++i) f.press({menu::Key::Accept});
    CHECK(f.page() == "UsedCarRoot");
    if (f.page() != "UsedCarRoot") return;
    for (int i = 0; i < 60; ++i) f.rt->update();
    menu::LiveWidget& page = *f.rt->current_page();
    menu::LiveWidget* pinch = named(page, "pinch");
    menu::LiveWidget* chip = f.rt->focused() ? named(*f.rt->focused(), "chip") : nullptr;
    CHECK(pinch && chip);
    if (!pinch || !chip) return;
    const std::optional<menu::Rect> top = pinch->data.rect("geometry");
    CHECK(top && top->h > 4 && top->h < 120);
    CHECK((u32(s32(chip->data.number("chip_0"))) >> 24 & 1) == 1);
    for (int i = 0; i < 12; ++i) f.press({menu::Key::Down});
    for (int i = 0; i < 120; ++i) f.rt->update();
    const std::optional<menu::Rect> down = pinch->data.rect("geometry");
    CHECK(top && down && down->y > top->y + 4 && std::fabs(down->h - top->h) < 0.5f);
    CHECK(f.clean());
}
