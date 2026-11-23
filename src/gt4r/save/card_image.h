#pragma once

// The saved game and a PlayStation 2 memory card image (an 8 MB card with or without its spare
// bytes: the `.ps2` / `.mcd` files of emulators and card tools): the game's folder taken from
// an image into the card folder under the data folder, and the card folder put into an image
// (a new card, or another card's image with its other saves kept). The card's own layout is the
// shared engine's (formats/ps2/memcard.h). docs/formats/save.md "A card image".

#include "gt4r/base/types.h"

#include <optional>
#include <string>
#include <vector>

namespace gt4r::save {

// The game's folder on a card: "BA" and the product code ("BASCUS-97328"), then "GAMEDATA".
std::string game_folder_name(const std::string& product = "BASCUS-97328");

struct CardImageResult {
    bool ok = false;
    std::string message; // what was done, or why not
    std::vector<std::string> folders; // the card's folders (to say what else it holds)
};

// Whether the bytes are a card image.
bool is_card_image(ByteSpan image);
// The game's folder of the image copied into `<card>/<folder name>/` (its files as they are; a
// folder already there is replaced). Not ok: not a card, no such folder on it.
CardImageResult import_card(ByteSpan image, const std::string& card, const std::string& product = "BASCUS-97328");
// The card folder's game as a card image. With `base`, that card's other folders are kept and
// the game's replaces its own; else a newly formatted card. Empty when there is no game in the
// folder or it does not fit (`why` says which).
Bytes export_card(const std::string& card, std::optional<ByteSpan> base, std::string* why = nullptr, const std::string& product = "BASCUS-97328");

// A card image put in the card folder (`<card>/*.ps2`, `*.mcd`, the first by name that holds the
// game) stands for a card in the console: when the folder has no game of its own, the image's
// is copied in. False when nothing was taken.
CardImageResult import_dropped_card(const std::string& card, const std::string& product = "BASCUS-97328");

} // namespace gt4r::save
