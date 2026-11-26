#pragma once

// Gran Turismo mode's transfer from an earlier game (the Status page's Start, "Transfer game data"): the card manager's
// probes and transfers (isExistGT3Data / inheritGT3Data, isExistGT4pData / inheritGT4pData; 0x17A300 .. 0x17A6A8) over the
// saves found here (save/earlier_game.h) instead of a card's slots, and the player's choice among them, which the app asks
// for (screen_earlier_saves). The scripts' own flow (StatusRoot's inheritDataCheck) runs as it is: its questions, its
// messages, the flags it sets. docs/formats/save.md "Earlier games' saves".

#include "gt4r/save/earlier_game.h"

#include <optional>
#include <string>
#include <vector>

namespace gt4r::menu {

class Runtime;
struct GameNatives;
struct Object;

// The card manager's four natives on a manager made for `slot` (MMemoryCardManager(mode, slot)).
void install_earlier_saves(GameNatives& n, Object& manager, const std::string& mode, int slot, bool slot_given);

// What the transfer asks the player to choose from.
struct TransferChoice {
    save::EarlierGame::Kind kind = save::EarlierGame::Kind::Gt3; // what the game asks for
    std::string region;                                          // the program's
    std::vector<save::EarlierGame> found;
    std::string looked; // where GT3 Revisited's saves were looked for ("": nowhere, its folder is another app's here)
};
// The app's side: whether it answers the choice (RuntimeConfig::choose_earlier_save; else the first save found is taken:
// tests); the choice the transfer waits for (once); the answer (none: nothing chosen, the game goes on as with no save).
void set_transfer_chooser(Runtime& rt, bool on);
std::optional<TransferChoice> take_transfer_choice(Runtime& rt);
void answer_transfer_choice(Runtime& rt, std::optional<save::EarlierGame> game);

} // namespace gt4r::menu
