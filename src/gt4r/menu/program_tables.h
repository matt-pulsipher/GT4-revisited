#pragma once

// Name lists the game's program holds and the scripts read through MCarData: the car makers,
// the tuners, the countries and the car categories by the numbers in the car table's columns
// (GENERIC_CAR). They are found in the user's own program file by what they are (lists of a
// number and a name) and not by where a release has them. docs/formats/menu.md.

#include "gt4r/base/types.h"

#include <map>
#include <string>

namespace gt4r::menu {

struct ProgramTables {
    std::map<int, std::string> makers;     // 52: "isuzu"
    std::map<int, std::string> tuners;     // 56: "isuzu", 76: "hks"
    std::map<int, std::string> countries;  // 2: "JP"
    std::map<int, std::string> categories; // 0: "NORMAL", "RACING", "TUNING", "CONCEPT"

    // From the unpacked program (its bytes as loaded at `load_address`). Lists not found stay
    // empty.
    static ProgramTables find(ByteSpan image, u32 load_address);
    // From the packed program file of the disc (CORE.GT4); empty lists when it does not read.
    static ProgramTables from_core(ByteSpan core_file);
};

} // namespace gt4r::menu
