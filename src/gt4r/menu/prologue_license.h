#pragma once

// Gran Turismo 4 Prologue's license record (main::game.license, the program's mLicense: 48 units of 0x2E8 bytes from the
// object's +8) in the program's layout, and its part of the saved game. docs/formats/prologue.md "The license tests".
//
// A unit (0x3FDFC0 makes one): +0x00 its category (an index into the names at 0x504FD0: none, B, A, IB, IA, S, D, GREEN,
// ORANGE, BLUE, RED, PURPLE, COFFEE), +0x04 its medal (an index into the names at 0x505008: none, bronze, silver, gold;
// a result keeps the best, 0x3FE1A0), +0x08 its most points (0x3FE430), from +0x10 ten ranking entries of 0x48 bytes in
// time order (0x3FE250 inserts one; 0x3FDF80 empties one), +0x2E0 its number (not saved). An entry: +0x00 the name
// (16 bytes), +0x10 the time (ms; 359,999,999 none), +0x18 the date (u64 seconds counted from Julian day 0 at noon:
// 0x4762A8), +0x20 the run's points, +0x24 the pass code (32 bytes; "--------" none).

#include "gt4r/base/types.h"

#include <array>
#include <string>
#include <vector>

namespace gt4r::menu {

struct Object;
struct RuntimeConfig;

constexpr size_t kPrologueLicenseUnits = 48;
constexpr size_t kPrologueLicenseUnitBytes = 0x2E0; // what the save writes of each unit (0x3FE998)

// The 48 units as the saved game holds them (48 x 0x2E0 bytes, the save's 0x3FF190 over 0x3FE998); empty when `game`
// has no Prologue license record.
std::vector<u8> prologue_license_units(const Object& game);
// Reads them back into the record (the load's part, 0x402288); false when `game` has no Prologue license record or the
// size is not 48 x 0x2E0.
bool prologue_license_load(Object& game, ByteSpan units);

// main::game.license on `game` (once). The names of the categories and medals are the program's (`config`'s program,
// when it is Prologue's); without them a unit's category and medal are kept by number only.
void install_prologue_license(Object& game, const RuntimeConfig* config = nullptr);

// The time as the program writes it (0x409D88): M'SS.mmm, the dashes for none.
std::string prologue_time_text(u32 ms);

} // namespace gt4r::menu
