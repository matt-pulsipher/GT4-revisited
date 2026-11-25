#pragma once

#include "gt4r/app/app.h"

#include <functional>
#include <memory>
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

} // namespace gt4r
