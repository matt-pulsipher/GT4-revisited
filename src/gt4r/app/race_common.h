#pragma once

// What the race screens share (defined in screen_race.cpp): the screen settings, the races' play list.

#include "gt4r/app/app.h"
#include "gt4r/audio/race_playlist.h"
#include "gt4r/menu/game.h"
#include "gt4r/race/picture_settings.h"

#include <map>
#include <memory>
#include <string>

namespace gt4r {

// The screen settings of the racing views and of the replays and B-spec races, kept while the app runs.
race::PictureSettings& picture_settings(bool replay);
// The screen settings from the game's options at a race's start, and what the pause changed back into them after it.
void pictures_from_options(const std::map<std::string, menu::OptionValue>& options);
void pictures_to_options(std::map<std::string, menu::OptionValue>& options);
// The game's play list of the races' music as the race's sound takes it (null: none filled), and what a race leaves of it.
std::shared_ptr<audio::RacePlaylist> playlist_from(const menu::GameState::PlayList& list);
void playlist_to(const audio::RacePlaylist& p, menu::GameState::PlayList& list);
// The play list of the races' music a new game has (null when the menus cannot be booted).
std::shared_ptr<audio::RacePlaylist> new_game_playlist(App& app);

// The two-player battle (screen_split.cpp): from the menus, or scripted (GT4R_RACE with players=2).
std::unique_ptr<Screen> make_split_screen(App& app, const RaceStart* chosen);

} // namespace gt4r
