#pragma once

// Gran Turismo 4's program tables that GT4R reads, written out as values (program_tables.cpp): every table the simulation,
// the computer drivers and the race's rules read of the program, each as a struct with named fields packed into the table's
// bytes. They are the tables of SCUS-97328 v1.01's program and serve every Gran Turismo 4 disc (sim::Program); each table is
// named by its address in that build, the address the readers give. A disc's own program is checked against them
// (check_tables): on v1.01 at those addresses, on any other build where its routines address them (program_code.h), and a
// table that differs or is not found is logged. Prologue's are program_prologue.h's.
//
// Each release has its own where its values differ (Release; docs/specs/physics.md "The releases"): the tables are made
// of SCUS-97328 v1.01's with the release's own values written over them, every table still named by its v1.01 address. A
// release's table may lack bytes v1.01's has (a gap: served as 0, absent in that program) and may hold step counts at its
// own step rate (Europe's 50 a second: served at 60 a second, the same in seconds, and checked so).

#include "gt4r/base/types.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gt4r::sim {

// The Gran Turismo 4 releases told apart by their programs.
enum class Release : u8 {
    Us,     // SCUS-97328 v1.01 (NTSC-U/C)
    Europe, // SCES-51719 v1.00 (PAL; its program steps 50 times a second)
    Japan,  // SCPS-17001 v1.05 (NTSC-J)
};
const char* release_name(Release r); // "SCUS-97328 v1.01"
// The release whose program `image` (loaded at `load`) is, by its code (the routines reference_build recognizes, as each
// release has them); none for any other build.
std::optional<Release> release_of(ByteSpan image, u32 load);

struct ProgramTable {
    const char* what; // "the static physics block"
    u32 address;      // its place in SCUS-97328 v1.01
    ByteSpan bytes;   // ours
    bool contains(u32 a) const { return a >= address && a - address < bytes.size(); }
};
// Every table of a release, by address (none overlaps the next).
std::span<const ProgramTable> gt4_tables(Release r = Release::Us);
// The table holding v1.01 address `a` (null: none).
const ProgramTable* gt4_table_at(u32 a, Release r = Release::Us);
// A hash of every table's address and bytes (a disc-free test pins each release's).
u64 gt4_tables_hash(Release r = Release::Us);
// How many bytes of a release's tables are its own (differ from v1.01's).
size_t gt4_release_bytes(Release r);

// A disc's program against our tables.
struct TableCheck {
    struct Row {
        const ProgramTable* table = nullptr;
        u32 found = 0;            // the table's start in the disc's program (0: not found)
        const char* how = "";     // "v1.01's place", "its routine", "not found"
        u32 routine = 0;          // the routine that placed it (its routine)
        u32 differing = 0;        // bytes unlike ours
        u32 first_differing = 0;  // the first of them (its offset in the table)
        u32 relocated = 0;        // words that are addresses, the program's own for ours (not counted as differing)
        u32 scaled = 0;           // step counts at the release's own step rate, equal to ours in seconds
        u32 gap = 0;              // bytes of ours the release's table lacks
    };
    bool reference = false; // the program is SCUS-97328 v1.01's (its code)
    std::optional<Release> release; // the release the program is (its tables compared); none: another build, v1.01's compared
    std::vector<Row> rows;
    size_t not_found() const;
    size_t differ() const;
    // One line per table that is not found or differs (`every`: per table), then a summary line.
    std::vector<std::string> lines(bool every = false) const;
};
// `image` loaded at `load` against its release's tables: SCUS-97328 v1.01's at their places; another build's found by the
// routines that address them (`locate` forces that on v1.01 too).
TableCheck check_tables(ByteSpan image, u32 load, bool locate = false);
// Whether the image holds SCUS-97328 v1.01's code (the routines that address the tables, as they are there).
bool reference_build(ByteSpan image, u32 load);

} // namespace gt4r::sim
