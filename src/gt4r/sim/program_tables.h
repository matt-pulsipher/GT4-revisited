#pragma once

// Gran Turismo 4's program tables that GT4R reads, written out as values (program_tables.cpp): every table the simulation,
// the computer drivers and the race's rules read of the program, each as a struct with named fields packed into the table's
// bytes. They are the tables of SCUS-97328 v1.01's program and serve every Gran Turismo 4 disc (sim::Program); each table is
// named by its address in that build, the address the readers give. A disc's own program is checked against them
// (check_tables): on v1.01 at those addresses, on any other build where its routines address them (program_code.h), and a
// table that differs or is not found is logged. Prologue's are program_prologue.h's.

#include "gt4r/base/types.h"

#include <span>
#include <string>
#include <vector>

namespace gt4r::sim {

struct ProgramTable {
    const char* what; // "the static physics block"
    u32 address;      // its place in SCUS-97328 v1.01
    ByteSpan bytes;   // ours
    bool contains(u32 a) const { return a >= address && a - address < bytes.size(); }
};
// Every table, by address (none overlaps the next).
std::span<const ProgramTable> gt4_tables();
// The table holding v1.01 address `a` (null: none).
const ProgramTable* gt4_table_at(u32 a);
// A hash of every table's address and bytes (a disc-free test pins it).
u64 gt4_tables_hash();

// A disc's program against our tables.
struct TableCheck {
    struct Row {
        const ProgramTable* table = nullptr;
        u32 found = 0;            // the table's start in the disc's program (0: not found)
        const char* how = "";     // "v1.01's place", "its routine", "not found"
        u32 routine = 0;          // the routine that placed it (its routine)
        u32 differing = 0;        // bytes unlike ours
        u32 first_differing = 0;  // the first of them (its offset in the table)
    };
    bool reference = false; // the program is SCUS-97328 v1.01's (its code)
    std::vector<Row> rows;
    size_t not_found() const;
    size_t differ() const;
    // One line per table that is not found or differs (`every`: per table), then a summary line.
    std::vector<std::string> lines(bool every = false) const;
};
// `image` loaded at `load`: SCUS-97328 v1.01's tables at their places; another build's found by the routines that address
// them (`locate` forces that on v1.01 too).
TableCheck check_tables(ByteSpan image, u32 load, bool locate = false);
// Whether the image holds SCUS-97328 v1.01's code (the routines that address the tables, as they are there).
bool reference_build(ByteSpan image, u32 load);

} // namespace gt4r::sim
