#pragma once

// The saved game on a memory card, kept as the card's folder under the data folder
// (docs/formats/save.md): <card>/BASCUS-97328GAMEDATA/ with the game data file of the same
// name, the garage file, icon.sys and icon0.ico, each as the game writes it, so a folder taken
// from a real card reads here and the other way round. Prologue's folder (BISCPS-15055GAMEDATA)
// holds its game data file, icon.sys and icon0.ico.

#include "gt4r/save/game_data.h"

#include <functional>
#include <optional>
#include <string>

namespace gt4r::save {

// The card manager's results (0x1CBF40): what the scripts compare.
enum class Result { Ok, DeviceError, InvalidFile, FileNotFound, DifferentMemoryCard, NoCard, NoOperation };
const char* result_name(Result r);

class GameSave {
public:
    using Reader = std::function<std::optional<Bytes>(const std::string& path)>;
    // `card`: the card's folder ("" : no card). `disc`: the disc's files (the icons).
    GameSave(std::string card, Codes codes, Reader disc = {}, std::string product = "BASCUS-97328");

    bool connected() const { return !card_.empty(); }
    const std::string& folder() const { return folder_; } // the save's folder, with its slash
    // 0 the game's data is there, 1 not, -1 the card cannot be read (the program's isExist).
    int exists() const;
    // KB the save uses on the card (readSize), -1 none; KB a new one needs (the file's `size`).
    int used_kb() const;
    int needed_kb() const;
    // saveNew: a new garage key, the garage file, the game data, the icons. `title`: the
    // save's second line in icon.sys.
    Result save_new(menu::GameState& s, const std::string& title);
    // save: the garage header (money, serials) and the game data; the cars' records to their
    // slots.
    Result save(menu::GameState& s);
    // load: the game data into the state, the garage header (money) and the cars' records.
    Result load(menu::GameState& s);
    // The game data alone (loadInstance): into `into`, without the garage file.
    Result load_instance(menu::GameState& into) const;
    Result remove();

private:
    std::string card_, folder_, name_;
    // Gran Turismo 4 Prologue's save ("BI" product): its own payload (save/prologue_data.h) as
    // the file, no garage file, its disc's icon.
    bool prologue_ = false;
    Codes codes_;
    Reader disc_;
    u32 stamp_ = 0; // the garage header's stamp last seen (0: none yet)

    std::optional<Bytes> read(const std::string& file) const;
    std::string icon_path() const; // the disc's icon for the save
    Result save_prologue(menu::GameState& s, const std::string* title); // a new save with a title, else written over
    bool write(const std::string& file, ByteSpan data) const;
    Result update_header(menu::GameState& s, u32 serial, bool money);
};

} // namespace gt4r::save
