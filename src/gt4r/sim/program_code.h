#pragma once

// Gran Turismo 4's code reduced to what does not depend on where a build put things, to find a routine in any build of the
// program and the table it addresses (program_tables.h).
//
// Every instruction is "masked": a jump's target cleared; the upper half of an address an instruction pair builds cleared,
// and the lower half of every instruction that adds to it, loads or stores through it, also after an index has been added
// to the upper half (a table read by element: upper half, add the index, load at the lower half). A routine is known by a
// hash of its masked instructions wherever it sits; the address an instruction builds is decoded from the two halves. No
// loaded value enters the hash (a build with other values in its tables hashes the same). Only hashes and instruction
// positions are kept in the sources, no code of any build.

#include "gt4r/base/types.h"

#include <vector>

namespace gt4r::sim {

class ProgramCode {
public:
    // `program`: the image loaded at `load_address`.
    ProgramCode(ByteSpan program, u32 load_address);

    u32 load() const { return load_; }
    u32 text_end() const { return text_end_; } // the end of the code (after the last return before 128 KB without one)
    bool in_code(u32 addr, u32 words = 1) const { return addr >= load_ && addr % 4 == 0 && (addr - load_) / 4 + u64(words) <= masked_.size(); }
    // The hash of the masked instructions [at, at + 4 words).
    u64 hash(u32 at, u32 words) const;
    // Every code address where `words` masked instructions hash to h.
    std::vector<u32> find(u64 h, u32 words) const;
    // The address the instruction at `pc` builds, loads or stores at from an upper half (0: none).
    u32 address_at(u32 pc) const;
    // The address the routine at `at` addresses at its instruction `index`, less `offset` (0: none there).
    u32 address_in(u32 at, u32 index, u32 offset) const;

private:
    u32 load_ = 0, text_end_ = 0;
    std::vector<u32> masked_, address_;
    std::vector<u64> prefix_, power_;
};

} // namespace gt4r::sim
