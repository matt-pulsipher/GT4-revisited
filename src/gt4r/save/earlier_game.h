#pragma once

// The earlier games' saves Gran Turismo mode's Status page reads (Start: "Transfer game data"): Gran Turismo 3's for credits
// and, by region, Gran Turismo 3's (the US program) or Gran Turismo 4 Prologue's for licenses; what the program reads of them
// (0x1D4448 .. 0x1D4AC4) and where they are here: GT3 Revisited's saves, a PlayStation 2 memory card image, a card's save
// folder or its data file, our own library's Prologue games. Read only. docs/formats/save.md "Earlier games' saves".

#include "gt4r/base/types.h"

#include <ctime>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::save {

struct EarlierGame {
    enum class Kind { Gt3, Prologue };
    Kind kind = Kind::Gt3;
    std::string name;   // the save's own name (GT3 Revisited's), else the player's, else the file's
    std::string from;   // what it is: "GT3 Revisited", "PS2 memory card image", ...
    std::string where;  // its file (or folder)
    std::time_t when = 0;
    bool current = false; // GT3 Revisited's game being played
    // What the program reads. Gran Turismo 3: its credits (the data file's u64 at 0x50, stored inverted) and the first 16
    // license tests' medal bytes (B 1 .. 8, A 1 .. 8: 0 gold, 1 silver, 2 bronze, 0xFF none; the low byte of each test's
    // medal word). Prologue: the first 25 license units' medal bytes (+4: 0 none, 1 bronze .. 3 gold).
    s64 credits = 0;
    std::vector<u8> medals;
};

// The card folders the program looks for, by its region string (0x68BB20: "us" here): Gran Turismo 3's (0x1D4598: "uk"
// BESCES-50294, "us" BASCUS-97102, any other BISCPS-15009) and the license probe's (0x1D4500: "uk" BESCES-52438, "us"
// BASCUS-97102 (Gran Turismo 3's), "kr" BKSCKA-20022, any other BISCPS-15055).
std::string gt3_folder(const std::string& region);
std::string license_folder(const std::string& region);
// Whether the license probe reads Gran Turismo 3 (the US program) rather than Prologue.
inline bool licenses_from_gt3(const std::string& region) { return region == "us"; }

// The program's rules (`region` the program's):
// inheritGT3Data (0x1D49C8): the credits added; nullopt when there are none (NO_OPERATION). Japan's sums are yen: / 100;
// at most 100,000.
std::optional<s64> transferred_credits(const EarlierGame& g, const std::string& region);
// inheritGT4pData (0x1D4698): the licenses opened, 0 none (NO_OPERATION), 1 B, 2 B and A. Gran Turismo 3 (the US
// program): its B license's 8 tests all passed (any medal) opens B, its A license's A. Prologue: its first 6 units passed
// open B, its first 25 both.
int transferred_licenses(const EarlierGame& g, const std::string& region);

// Gran Turismo 3's data file (the card folder's file of its name: a 0x40-byte header, version 7.x, the payload inverted;
// the minor is the release's: 0 Japan, 1 US, 2 Europe) as the program reads it. nullopt (and why) when it is none, or another
// release's than the region's.
std::optional<EarlierGame> read_gt3_data_file(ByteSpan file, const std::string& region, std::string* why = nullptr);
// A Gran Turismo 4 Prologue game's data file (save/prologue_data.h) as the program reads it (the Japanese layout: unit k's
// medal at 0x34 + 0x2E0 k).
std::optional<EarlierGame> read_prologue_data_file(ByteSpan file, std::string* why = nullptr);
// GT3 Revisited's own save (a "GT3R-GTMODE" text file).
std::optional<EarlierGame> read_gt3r_save(ByteSpan file, std::string* why = nullptr);
// A file the player chose: a GT3 Revisited save, a memory card image or .psu holding the region's Gran Turismo 3 folder, or
// that folder's data file. `name`: the file's name (a .psu by it; a picked file may have none).
std::optional<EarlierGame> read_earlier_file(ByteSpan bytes, const std::string& name, const std::string& region, std::string* why = nullptr);

// GT3 Revisited's saves of the region's Gran Turismo 3: in `gt3r_dir` (its data folder) the save folder its Settings chose
// ([saves] folder in its config.ini, else that folder), its library there (saves/ for the US release, saves/eu/, saves/jp/),
// its game being played first, then the newest; its first version's single gtmode.sav when it has no library yet.
// `looked`: the folder looked in.
std::vector<EarlierGame> find_gt3r_games(const std::string& gt3r_dir, const std::string& region, std::string* looked = nullptr);
// The region's Gran Turismo 3 folder in a card folder (copied from a card as Gran Turismo 4's is), and in the card images
// (*.ps2, *.mcd) there.
std::vector<EarlierGame> find_card_games(const std::string& card, const std::string& region);
// Our library's Prologue games (data folder; save_library.h), the region's (Japan's: BISCPS-15055) only.
std::vector<EarlierGame> find_prologue_games(const std::string& data_dir, const std::string& region);

} // namespace gt4r::save
