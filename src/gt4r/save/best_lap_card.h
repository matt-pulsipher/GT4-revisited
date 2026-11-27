#pragma once

// A best lap's replay on the memory card, as the game writes one (GT4MC::FileGT4ReplayBest, "GhostData"): a folder
// <product>B and seven letters holding the data file named like the folder (the race's GhostData: race/race_recording.h
// ghost_recording, scrambled as a replay), its particulars (`thumbnail`: save/replay_particulars.h), icon0.ico (the disc's
// replay icon by the race's kind, FileGT4ReplayBest::vf17: icon/GT4_Rep_to_license.ico, _to_race.ico, _tt1.ico) and icon.sys.
// docs/formats/save.md "The game's other saves".

#include "gt4r/save/memory_card.h"
#include "gt4r/save/replay_particulars.h"

#include <string>

namespace gt4r::save {

// Whether a card's folder name is a best lap's (the letter after the product: 'B').
bool is_best_lap_folder(const std::string& folder);

// Writes the folder into `card` (the card's folder); `data`: the GhostData file as the race writes it (not scrambled yet);
// `icon`: the disc's icon's path; `title`: icon.sys's second line. `folder` gets the folder's name. Over `over` (a folder's
// name, "": a new one): that one goes first.
Result write_best_lap(const std::string& card, const std::string& product, const GameSave::Reader& disc, ByteSpan data, const ReplayParticulars& p,
                      const std::string& icon, const std::string& title, const std::string& over, std::string* folder = nullptr);

} // namespace gt4r::save
