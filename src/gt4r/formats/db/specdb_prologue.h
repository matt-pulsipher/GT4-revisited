#pragma once

// Gran Turismo 4 Prologue's car database (specdb/GT4_PROLOGUE1000/): the same files as Gran
// Turismo 4's (tables .dbt, labels .idi, string pools .sdb) in an earlier layout. Its rows are
// stored plain, its parts' rows have other columns, four tables are missing (so the table ids
// from 27 on are four lower), and a car's paints are in one VARIATION table for every language.
// These read it and present it in Gran Turismo 4's layout, so everything that reads a SpecDb
// reads Prologue's the same way; docs/formats/prologue.md "The car database".

#include "gt4r/formats/db/specdb.h"

#include <string>

namespace gt4r::db::prologue {

// A folder of Prologue's layout ("specdb/GT4_PROLOGUE1000/").
bool is_prologue_folder(const std::string& folder);
// A table whose rows are stored plain (flags bit 0 clear): "GTDB", u32 flags, u32 rows, u32 row
// size, {u32 id, u32 offset}[rows], then the rows, each at its offset from the end of that list.
bool is_plain_table(ByteSpan file);
// Throws FormatError.
Table parse_plain(ByteSpan file);

// The file a table of Gran Turismo 4's name is read from ("VARIATIONjapanese": "VARIATION").
std::string file_name(const std::string& name);
// A Prologue table id as Gran Turismo 4 numbers it (27 TIRESIZE is 31, ...); -1 stays.
u32 table_id(u32 prologue_id);
// A table's rows in Gran Turismo 4's columns: the columns both have copied, the ids of other
// tables in the rows renumbered, the columns Prologue does not have zero or made from what it
// has (CHASSIS's tire widths). `tables` gives the other tables a column is made from. A table
// this does not know is returned as it is.
Table to_gt4(const std::string& name, const Table& t, SpecDb& tables);
// The labels with Gran Turismo 4's table id.
Labels to_gt4(Labels labels);
// The name of a race's file of opponents (specdb/<set>/RACES/<table><row>) for a RACE row:
// Prologue's files carry its own table number.
std::string races_file(u32 race_row);

} // namespace gt4r::db::prologue
