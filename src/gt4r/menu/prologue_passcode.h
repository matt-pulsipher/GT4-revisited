#pragma once

// Gran Turismo 4 Prologue's pass codes (docs/formats/prologue.md "The pass codes"): the 30 digits a record's entry keeps
// (a license run's, 0x19ED58 with 0x19EC08), made by 0x1A31A0 from ten fields.
//
// The fields are packed low bit first into 80 bits (0x1A4660): the record's kind (5 bits; 11 for a license run), the
// car (9: the place of its row in the database's table 0, 0x40A270), the course (8: its row's place in table 30,
// 0x40A2A0), a flag (1), the test's number (12: the race code's digits, 0x19F238), two words (12 and 3), the time (22,
// ms), two more (4 and 4). 0x1A2FA8 puts them after two zero bytes (12 bytes), writes into the first 4 bits a nibble of a
// 32-bit hash of the program's word 0x5057C0 (0 in every game seen) and into the next 12 a fold of the CRC-32 of the 12
// bytes, then enciphers the 12 bytes with SAFER SK-128 of 13 rounds under the program's key (0x55CAD0, 0x55CAD8), an
// 8-byte block at each offset 0 .. 4 in turn (0x1A3E70). 0x1A48B0 writes the 12 bytes, a little-endian number, as 30
// decimal digits (leading zeros); the first is then a check digit (from -1: x 3 plus each later digit's character, mod
// 10). The hash (0x1A2EA8) is a Davies-Meyer-like double-block one over SAFER SK-128 of 10 rounds.

#include "gt4r/base/types.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::menu {

struct PassFields {
    u32 kind = 11, car = 0, course = 0, flag = 0, test = 0, word14 = 0, word18 = 0, time = 0, word20 = 0, word24 = 0;
};

// How a game packs its fields: their widths (low bit first), the bytes they fill, the block enciphered (the fields after
// two zero bytes) and the digits written. Prologue's: 80 bits in 10 bytes, 12, 30 digits. Gran Turismo 4's (0x44B258 ->
// 0x44B060, 0x44C918): ten fields of 5, 11, 9, 1, 12, 12, 3, 22, 4, 4 bits (83) in 11 bytes, 13 enciphered, 32 digits with
// no check digit over the first.
struct PassLayout {
    std::vector<u32> widths;
    size_t packed = 10, block = 12, digits = 30;
    bool check = true;
    static PassLayout prologue() { return {{5, 9, 8, 1, 12, 12, 3, 22, 4, 4}, 10, 12, 30, true}; }
    static PassLayout gt4() { return {{5, 11, 9, 1, 12, 12, 3, 22, 4, 4}, 11, 13, 32, false}; }
};

class PassCoder {
public:
    // The key from the program's image (Prologue's, loaded at `load`); none when it cannot be read.
    static std::optional<PassCoder> from_program(ByteSpan image, u32 load);
    PassCoder(const std::array<u8, 8>& key_a, const std::array<u8, 8>& key_b, PassLayout layout = PassLayout::prologue());
    // 0x1A31A0: the 30 digits. `seed` the program's word 0x5057C0.
    std::string encode(const PassFields& f, u32 seed = 0) const;
    // The fields by the layout's widths, in order.
    std::string encode(const std::vector<u32>& values, u32 seed = 0) const;
    // The other way: a code's fields (by the layout's widths), none when its digits or its CRC do not hold.
    std::optional<std::vector<u32>> decode(const std::string& digits) const;

private:
    std::array<u8, 8> key_a_, key_b_;
    PassLayout layout_;
};

} // namespace gt4r::menu
