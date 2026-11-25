#pragma once

// The running menus' pages drawn over something else (the race's menus over the race): the
// open pages of a menu::Runtime with their pointer and the transition's cover, as the menu
// viewer draws them (screen_menu_viewer.cpp).

#include "gt4r/app/app.h"
#include "gt4r/menu/runtime.h"

#include <memory>

namespace gt4r {

class MenuPainter {
public:
    virtual ~MenuPainter() = default;
    // The pages' 640 x 480 at (x, 0), `scale` screen units a page unit. Nothing behind them is cleared.
    virtual void draw(menu::Runtime& menus, float x, float scale) = 0;
};
std::unique_ptr<MenuPainter> make_menu_painter(App& app);

// The running menus' keys from the pad as the menu viewer gives them.
void send_menu_keys(menu::Runtime& menus, const MenuInput& m);
// Its directions alone; `on` false: they are taken by something else.
void send_menu_directions(menu::Runtime& menus, const MenuInput& m, bool on);
// The pad as the menus' scripts read it (Runtime::set_pad).
menu::Runtime::Pad menu_runtime_pad(const gt3r::PadState& p);

} // namespace gt4r
