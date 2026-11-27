#pragma once

// Gran Turismo 4 Prologue's program tables that GT4R reads, written out as values (program_tables_prologue.cpp): every table of
// the program its race, computer drivers, converter and race display read, each area a struct with named fields packed into
// the area's bytes (program_tables_prologue_values.inc), named by its address in SCPS-15055 v1.01, the build program_prologue.h
// recognizes. sim::Program serves them on a Prologue disc (never the shared engine's or Gran Turismo 4's); the disc's own program
// is compared with them once and the result logged. A license test's rules, which the program makes in code (a switch on the
// test's number, 0x3E1C20), are values too.

#include "gt4r/sim/program_tables.h"

#include <array>
#include <span>

namespace gt4r::sim {

// Every area, by address (none overlaps the next).
std::span<const ProgramTable> prologue_tables();
// The area holding address `a` (null: none).
const ProgramTable* prologue_table_at(u32 a);
// A hash of every area's address and bytes (a disc-free test pins it).
u64 prologue_tables_hash();
// A Prologue program's image (loaded at `load`) against our areas, at their places (rows as TableCheck's; `recognized`: the
// build program_prologue.h places, else every row "not found").
TableCheck check_prologue_tables(ByteSpan image, u32 load, bool recognized);

// The race rules' values the program makes in its code (race_rules.h).
struct PrologueLessonRule {
    u8 speed_cap = 0;                      // the rule block's +0x42 (km/h; lesson 1's 50)
    u8 mileage = 0;                        // +0x45 (lesson 36's 100)
    std::array<u8, 5> start_speed{};       // +0x4F .. +0x53: cars 1 .. 5's start speed (km/h; 0 the row's)
    std::array<s16, 5> start_distance{};   // +0x56 + 2 k: their start distance (m)
};
// 0x3E1C20's cases (the table 0x572CC0), by the test's number 1 .. 36.
const std::array<PrologueLessonRule, 36>& prologue_lesson_rules();
// The penalty's speed cap (km/h): the constant 0x3C5B48 loads.
constexpr u8 kProloguePenaltyCap = 50;

} // namespace gt4r::sim
