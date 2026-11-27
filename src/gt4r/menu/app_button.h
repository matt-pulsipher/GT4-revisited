#pragma once
// The game's pages with a button of GT4 Revisited's own beside or over them (app/screen_menu_viewer.cpp draws and uses it):
//   - GT4R menu, which leaves the game for the app's own menu underneath it (the game saved first): beside the title (Arcade's
//     top page, arcade TopRoot; Prologue's board, prologue SchoolRoot, which is also its School Mode's home) and Gran Turismo
//     mode's home, its map (gtmode TopRoot), where its races and pages come back to;
//   - Controller Settings, in place of the Options' controller pages' tables (option KeyconfigRoot, ControllerRoot,
//     SteeringRoot): the app's own controls over the game.

#include <string_view>

namespace gt4r::menu {

enum class AppButton { None, Menu, Keys };

inline AppButton app_button(std::string_view project, std::string_view page) {
    if ((project == "arcade" && page == "TopRoot") || (project == "prologue" && page == "SchoolRoot") || (project == "gtmode" && page == "TopRoot"))
        return AppButton::Menu;
    if (project == "option" && (page == "KeyconfigRoot" || page == "ControllerRoot" || page == "SteeringRoot")) return AppButton::Keys;
    return AppButton::None;
}

// Whether Select does what the button does, on a page with one: only where the page's scripts do nothing with Select.
// Arcade's top page has no use for it; Prologue's board takes it while the focus is in its ring of signs (`in_ring`: between
// the status and the tests); Gran Turismo mode's map reads it itself (its key handler compares the key with Select's number,
// 0xFF63), so there only a click or a tap leaves, and a pad reaches the app's menu through the title. The controller pages:
// Select opens Controller Settings.
inline bool select_is_app_button(std::string_view project, std::string_view page, bool in_ring) {
    switch (app_button(project, page)) {
    case AppButton::Keys: return true;
    case AppButton::Menu: return project == "arcade" || (project == "prologue" && !in_ring);
    default: return false;
    }
}

// Where the GT4R menu button goes over the page when the window has no room beside or under it: its top in page units (480
// high), over the bottom left corner: on the title above its copyright line; on the map in the band of its scrolling help
// line (page y 440 to 480, under the map's scroll window, 640 x 440), clear of the map's places.
inline float app_button_top_over_page(std::string_view project, std::string_view page) {
    return project == "gtmode" && page == "TopRoot" ? 444.0f : 413.3f;
}

} // namespace gt4r::menu
