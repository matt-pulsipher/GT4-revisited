#pragma once

// The game data file's payload (docs/formats/save.md "The game data"): the program's Status
// object written as fixed blocks, read back into the menus' GameState. Codes of the car
// database ({u32 row, u32 table}) stand for the cars, courses, wheels and wings the state keeps
// by label; the caller turns them back and forth.

#include "gt4r/menu/game.h"

#include "gt4r/formats/db/specdb.h"

#include <functional>
#include <optional>
#include <string>

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

// The releases' payloads (docs/formats/save.md "Japan's game data"): the US and European programs' (the layout above) and
// the Japanese program's (SCPS-17001 v1.05), whose options block is 0x11D8 bytes (0x11C0 here): the screen settings at
// Option +0x1180 and +0x1190 and the slide show's transition time at +0x11A0 (here +0x1184, +0x1194, +0x1180), and the
// blocks after it 0x20 bytes later (a payload of 0x3A0A0). The state is read and written in the layout above and turned
// into the release's at the file.
enum class Layout : u8 { Us, Japan };
Layout layout_of_product(const std::string& product); // "BISCPS-17001": Japan; any other the US layout
size_t payload_size(Layout l);
// A payload of the layout above as the release's program writes it (its header's length and CRC filled in).
Bytes to_layout(ByteSpan payload, Layout l);
// A release's payload in the layout above; none when it is not that release's whole game data (valid_game's checks).
std::optional<Bytes> from_layout(ByteSpan payload, Layout l);
// The payload into the state. The cars' records other than the one driven are not in it (the
// garage file's slots); their entries keep their "has a record" mark for the caller to fill.
// False (the state untouched) when it is not valid.
bool read_game(ByteSpan payload, menu::GameState& state, const Codes& codes, std::vector<int>* records_wanted = nullptr);

} // namespace gt4r::save
