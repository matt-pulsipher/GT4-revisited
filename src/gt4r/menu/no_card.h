#pragma once

// The game without a memory card to be missing: saves are folders of the data folder (save/save_library.h), so the scripts'
// questions and warnings about a card are answered here, before their page is opened; what a page must say about a card is said
// about the save; a write that really fails is told once, with its folder; and the game being played is saved by itself
// (docs/formats/save.md "Without a card"). Not with RuntimeConfig::console_card, which has a console's card and its dialogs.

#include "gt4r/menu/runtime.h"

#include <optional>
#include <string>

namespace gt4r::menu {

// What the common confirm dialog answers without being shown, or what it says instead.
struct CardAnswer {
    int answer = 0;
    std::string shown; // "" : not shown; else this, as a notice with OK, and then `answer`
};
// For a message as a script made it (its text's template of the disc, whatever the numbers in it). None: the dialog as it is.
std::optional<CardAnswer> card_answer(Runtime& rt, const std::string& message);
// Whether `text` is `pattern` with its %d and %s filled in.
bool matches_template(const std::string& text, const std::string& pattern);

// The texts of a project that speak of a card, in our words (the English ones; another language keeps its own).
void reword_card_texts(Project& project, const std::string& language);

// Saving by itself: when the game has changed since it was last saved or loaded (the options' autosave on). Called where no
// script is running.
void autosave(Runtime& rt);
// The game being played read from its folder, as the boot does (Prologue, whose boot's project is not run). False: none there,
// or not readable (told once).
bool load_saved_game(Runtime& rt);
// The game saved now; "" or why not.
std::string save_game_now(Runtime& rt);

} // namespace gt4r::menu
