#pragma once

#include "gt4r/app/app.h"
#include "gt4r/menu/earlier_saves.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace gt4r {

// What the saved games' page does to the game that runs under it (none: the home screen's, which only chooses the game Play
// goes on with).
struct SavesHooks {
    std::function<std::string()> save_now;                  // "" saved, else why not
    std::function<void(const std::string& card)> load;      // the game started again with this one
    std::function<void(const std::string& card)> new_game;  // started again with a new one, to be made in this folder
};
std::unique_ptr<Screen> make_saves_screen(App& app, SavesHooks hooks = {});

// The Status page's transfer from an earlier game: the player's choice of its save (menu/earlier_saves.h; screen_earlier_saves.cpp).
std::unique_ptr<Screen> make_earlier_saves_screen(menu::TransferChoice choice, std::function<void(std::optional<save::EarlierGame>)> answer);
// GT3 Revisited's data folder on this computer: GT4R_GT3R_DIR, else its own default folder; "" when there is none (or it is
// another app's, on Android and iOS).
std::string gt3r_data_dir();

} // namespace gt4r
