#pragma once

// A saved game as the user's own program reads it (docs/formats/save.md): the game data file through its decoder (0x1D4E60),
// the Status's check (0x43AA68) and read (0x43ABA0), and what its write (0x43AAF0) then makes of it; the garage file's header
// through 0x1CB9B8 and each slot through the routines of 0x1CEEC0 (the bit shuffle's way back, the cipher's, the CRC).
// Prologue's: the check (0x4021A8), the load (0x402288) and the save (0x4021F0). Routines are named by address; nothing of the
// program is kept.

#include "gt4r/base/types.h"
#include "gt4r/disc/disc.h"

#include <string>
#include <vector>

namespace gt4r::diff {

struct SaveCheck {
    bool ran = false;
    bool decoded = false;  // the decoder took the file
    bool valid = false;    // the check took the payload
    size_t differ = 0;     // bytes the program writes differently from what it read
    size_t first = 0;      // the first of them
    bool garage = false;   // the header's key is the game's, a serial the game's
    s64 money = 0;
    std::vector<int> slots_bad; // asked for, not whole
    bool ok() const { return ran && decoded && valid && differ == 0 && garage && slots_bad.empty(); }
};
// `slots`: the garage places whose slots hold a car's record.
SaveCheck check_save(Disc& disc, const Bytes& game, const Bytes& garage, const std::vector<int>& slots);
SaveCheck check_prologue_save(Disc& disc, const Bytes& game);

// gt4r_diff <disc> save <folder or card image>
int run_save_check(Disc& disc, const std::string& path);

} // namespace gt4r::diff
