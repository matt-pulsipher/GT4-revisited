#pragma once

// An opponent's car as the race enters it (docs/specs/ai.md "Who races"): the car of its ENEMY_CARS row with that
// row's set of parts (DEFAULT_PARTS) and settings (DEFAULT_PARAM), and the paint its pool gives it.

#include "gt4r/formats/db/race_events.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/equipment.h"
#include "gt4r/sim/program.h"

#include <optional>

namespace gt4r::sim::ai {

// A DEFAULT_PARAM row (37 bytes): the settings an opponent's car is entered with (0x444860 copies them into the
// equipment's settings as they are, without the parts' ranges).
struct EntrySettings {
    u16 output = 0;          // +0x00 the engine's output factor, per mille (into +0x15E: an entered car only)
    u16 final_drive = 0;     // +0x02 (x 1000)
    u16 ride[2] = {};        // +0x04 / +0x06 front / rear ride height, mm
    u8 spring[2] = {};       // +0x08 / +0x0A (+0x09 / +0x0B unread)
    u8 downforce[2] = {};    // +0x0C / +0x0D
    u8 lsd[6] = {};          // +0x0E .. +0x13: front initial, acceleration, deceleration, then the rear's
    u8 gear_slider = 0;      // +0x14 a computed gearbox's design top speed / 10 km/h
    u8 camber[2] = {};       // +0x15 / +0x16
    u8 toe[2] = {};          // +0x17 / +0x18
    u8 dampers[8] = {};      // +0x19 .. +0x20 in the equipment's order
    u8 anti_roll[2] = {};    // +0x21 / +0x22
    u8 ascc = 0;             // +0x23 the first ASCC index
    u8 tcs = 0;              // +0x24 the TCS index
    static std::optional<EntrySettings> read(ByteSpan row);
    // 0x444860: into an equipment (the output factor only when `entered`: the record names an ENEMY_CARS row).
    void apply(Equipment& eq, bool entered) const;
};

// 0x444690: the equipment of an ENEMY_CARS row: its car, the row's parts with each part's default settings, the
// row's settings, a computed gearbox's ratios from them (0x443240), the record's +0x08 naming the row; then
// 0x445880: `paint` when the car has that many (-1: 0xFF, the race's choice). False when a row is missing.
bool opponent_equipment(db::SpecDb& db, const Program& program, const db::Opponent& o, Equipment& out);

// A grid entry's equipment: an opponent drawn from a pool as above, any other car as sold (0x444480) with its paint.
bool entry_equipment(db::SpecDb& db, const Program& program, const db::RaceEvents& events, const GridEntry& g, Equipment& out);

// The number of paints of an equipment's car (0x445808: its rows of the paint table).
int paint_count(db::SpecDb& db, const Equipment& eq);

} // namespace gt4r::sim::ai
