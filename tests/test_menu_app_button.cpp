// The game's pages with a button of the app's own (menu/app_button.h): GT4R menu beside the title and Gran Turismo mode's map,
// Controller Settings on the Options' controller pages, and where Select does the same.

#include "test_framework.h"

#include "gt4r/menu/app_button.h"

using namespace gt4r::menu;

TEST(menu_app_button_pages) {
    CHECK(app_button("arcade", "TopRoot") == AppButton::Menu);     // the title
    CHECK(app_button("prologue", "SchoolRoot") == AppButton::Menu); // Prologue's board, School Mode's home
    CHECK(app_button("gtmode", "TopRoot") == AppButton::Menu);     // Gran Turismo mode's map
    CHECK(app_button("arcade", "GameRoot") == AppButton::None);
    CHECK(app_button("gtmode", "HomeRoot") == AppButton::None);
    CHECK(app_button("event", "TopRoot") == AppButton::None);
    CHECK(app_button("option", "KeyconfigRoot") == AppButton::Keys);
    CHECK(app_button("option", "ControllerRoot") == AppButton::Keys);
    CHECK(app_button("option", "SteeringRoot") == AppButton::Keys);
    CHECK(app_button("option", "TopRoot") == AppButton::None);
}

TEST(menu_app_button_select) {
    CHECK(select_is_app_button("arcade", "TopRoot", false));
    CHECK(select_is_app_button("prologue", "SchoolRoot", false));
    CHECK(!select_is_app_button("prologue", "SchoolRoot", true)); // (the ring of signs takes Select)
    CHECK(!select_is_app_button("gtmode", "TopRoot", false));     // (the map's script reads Select)
    CHECK(select_is_app_button("option", "KeyconfigRoot", false));
    CHECK(!select_is_app_button("arcade", "GameRoot", false));
    // Over the page: the title's button above its copyright line, the map's in its help line's band (page y 440 to 480).
    CHECK(app_button_top_over_page("arcade", "TopRoot") + 29 <= 445);
    CHECK(app_button_top_over_page("gtmode", "TopRoot") >= 440 && app_button_top_over_page("gtmode", "TopRoot") + 29.4f <= 480);
}
