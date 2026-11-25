#pragma once

// The saved games of a data folder: any number of them, each in a folder as a memory card's is (save/memory_card.h), the first in
// `memorycard/`, the others in `games/<number>/`; `games/current-<product>` names the one being played. There is no card the player
// could be missing. docs/formats/save.md "Several games".

#include "gt4r/save/memory_card.h"

#include <ctime>
#include <string>
#include <vector>

namespace gt4r::save {

struct SavedGame {
    std::string card; // its folder (what GameSave is made with)
    bool current = false, readable = false;
    std::string name; // the player's
    s64 money = 0;
    int day = 0, cars = 0;
    std::time_t when = 0; // the file's
};

class SaveLibrary {
public:
    // `product`: "BASCUS-97328", Prologue's "BISCPS-15055" (each disc's games apart from the other's).
    SaveLibrary(std::string data_dir, std::string product);
    bool ok() const { return !dir_.empty(); }
    // The folder of the game being played (the first one's when none is named or the one named is gone; a folder without a
    // game is where a new one goes).
    std::string current() const;
    bool set_current(const std::string& card);
    // The folders that hold a game, the newest first.
    std::vector<std::string> cards() const;
    // A folder without a game, for a new one (not made yet).
    std::string new_card() const;
    bool holds_game(const std::string& card) const;
    // A game of the library taken away (its own files alone; its folder when nothing else is in it).
    bool remove(const std::string& card);
    // What the list shows.
    std::vector<SavedGame> games(const Codes& codes) const;
    // One of them opened for reading and writing as the program does (load, save: its checks and scrambling). `disc`: the disc's
    // files, for the icons of a new one.
    GameSave open(const std::string& card, Codes codes, GameSave::Reader disc = {}) const { return GameSave(card, std::move(codes), std::move(disc), product_); }
    // A mark of its files as they are now: another one later says that another part wrote the game (0: no game).
    u64 change_mark(const std::string& card) const;

private:
    std::string dir_, product_, game_; // the game's folder's name
    std::string marker() const;
};

} // namespace gt4r::save
