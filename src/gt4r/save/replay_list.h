#pragma once

// The Replay Theater's play list on a memory card, as the game writes it (GT4MC::FileGT4ReplayList): a folder
// <product>LSTRPLAY (FileGT4ReplayList::vf7 0x1D1228) holding the data file named like it, icon0.ico (the disc's
// icon/GT4_Playlist_replay.ico, vf17 0x1D1270) and icon.sys. The data is the list as the program keeps it (FileGT4PlayList,
// the slide show's layout too: vf3 0x1D0F38): 400 entries of u64, the count (u32) at 0xC80, padded with zeros to 0xCC0,
// not scrambled. An entry is a card folder: its seven letters as a number (0x1CBEB0: the name's letters from its 14th) with
// the letter after the product in the top byte ('R' a full replay, 'B' a best lap's; 0 for a name without the product:
// 0x1CD860, 0x1D1478). docs/formats/save.md "The Replay Theater's play list".

#include "gt4r/save/memory_card.h"

#include <optional>
#include <string>
#include <vector>

namespace gt4r::save {

inline constexpr int kPlayListSize = 400;       // (0x1D1280: an append past 400 does nothing)
inline constexpr size_t kPlayListBytes = 0xCC0; // the data file
inline constexpr int kPlayListKb = 76;          // the folder's size as the scripts are told it (the photos' list's too)

// The data file of a list (the first 400 entries) and back (none: too short, or a count past 400).
Bytes encode_play_list(const std::vector<u64>& entries);
std::optional<std::vector<u64>> decode_play_list(ByteSpan data);

// The entry of a card folder's name (<product>, its letter, seven letters); none when the letters do not read.
std::optional<u64> folder_entry(const std::string& folder, const std::string& product);

class ReplayList {
public:
    // `card`: the card's folder ("": none); `disc`: the disc's files (the icons).
    ReplayList(std::string card, GameSave::Reader disc, std::string product = "BASCUS-97328");
    std::string folder() const { return product_ + "LSTRPLAY"; }
    bool exists() const;
    Result load(std::vector<u64>& out) const;
    // The list written; `title`: icon.sys's second line (the folder's icon and icon.sys kept when there already).
    Result save(const std::vector<u64>& entries, const std::string& title) const;
    Result remove() const;

private:
    std::string card_, product_;
    GameSave::Reader disc_;
};

} // namespace gt4r::save
