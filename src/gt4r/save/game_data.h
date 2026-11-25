#pragma once

// The game data file's payload (docs/formats/save.md "The game data"): the program's Status
// object written as fixed blocks, read back into the menus' GameState. Codes of the car
// database ({u32 row, u32 table}) stand for the cars, courses, wheels and wings the state keeps
// by label; the caller turns them back and forth.

#include "gt4r/menu/game.h"

#include "gt4r/formats/db/specdb.h"

#include <functional>

namespace gt4r::save {

constexpr size_t kPayloadSize = 0x3A080;
constexpr u32 kPayloadMagic = 0xFF60BAD3;
// Places of the payload the memory card needs.
constexpr size_t kGarageKeyAt = 0x135E4, kGarageSerialAt = 0x135F8;

struct Codes {
    std::function<u64(u32 table, const std::string& label)> code; // ~0: none
    std::function<std::string(u64 code)> label;                    // "": none
};

// The codes of a disc's car database (labels by the table a code names).
Codes database_codes(db::SpecDb& db);

// A new game's payload: what the program's constructors make (the blocks the state does not
// name are taken from here when no saved game was loaded).
Bytes new_game_payload();
// A play list as the options keep it (0x810 bytes; a saved replay's header has the races' music's).
Bytes playlist_block(const menu::GameState::PlayList& list);
// The state as a payload (its header's length and CRC filled in).
Bytes write_game(const menu::GameState& state, const Codes& codes);
// Whether a payload is this program's game data: the magic, the length, the CRC (0x43AA68).
bool valid_game(ByteSpan payload);
// The payload into the state. The cars' records other than the one driven are not in it (the
// garage file's slots); their entries keep their "has a record" mark for the caller to fill.
// False (the state untouched) when it is not valid.
bool read_game(ByteSpan payload, menu::GameState& state, const Codes& codes, std::vector<int>* records_wanted = nullptr);

} // namespace gt4r::save
