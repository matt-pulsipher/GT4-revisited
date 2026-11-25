#pragma once

// Gran Turismo 4 Prologue's recorded runs (replay/JP/<event label>/<event label>: the 46 lessons' demonstrations and ten
// races'): Gran Turismo 4's Serialize::ReplayData (demonstration.h) one version earlier, its body stored complemented, "GT2K"
// in place of "GT4M", 16-byte pad records. docs/formats/prologue.md "The recorded runs".

#include "gt4r/formats/replay/demonstration.h"

namespace gt4r::replay::prologue {

// An entry of the race's header (0x1F0 bytes from +0x518, written from the race entry by RaceBase's virtual 21, 0x37C0F0).
struct Entry {
    std::array<u8, 0x140> equipment{}; // +0x000 the entry's equipment (entry +0x20): +0x10 the car {row, table}
    std::array<u8, 0x20> condition{};  // +0x140 the condition block (entry +0x3FCC)
    u32 x160 = 0;                      // +0x160 (entry +0x168) ❓
    s32 grid_place = 0;                // +0x164 (entry +0x3FA8)
    u8 pedal_style = 0;                // +0x168 (entry +0x3FAC; 0: automatic)
    u8 driver_kind = 0;                // +0x169 (entry +0x3FB0)
    u8 x16a = 0;                       // +0x16A (entry +0x3FB4) ❓
    u8 asm_on = 0, tcs_on = 0;         // +0x16B / +0x16C (entry +0x3FB8 / +0x3FBC)
    u64 code(size_t at) const;         // the {row, table} at +at of the equipment
};

struct Run {
    std::string race_class;        // +0x0C "RaceLicense"
    u64 event = 0, course = 0;     // +0x68 the RACE row (table 0x1F), +0x70 the COURSE row (table 0x1E)
    s32 time_ms = 0, time2_ms = 0; // +0x78 the first entry's time, +0x7C 0x37B4F8 of -1
    std::array<u8, 0x18> information{}; // +0x4C the race information's first 0x18 bytes
    std::vector<Entry> entries;    // +0x510 the count, +0x518 the entries
    std::vector<ControlStream> streams; // from +0x149C: u32 n (0: one stream; else n recorders, 0x1088 bytes then a stream)

    // nullopt (with *error) when the file is not a ReplayData of Prologue's. `bases`: the deltas' bases (the program's 0x5031D8).
    static std::optional<Run> parse(ByteSpan file, const std::array<s32, 8>& bases, std::string* error = nullptr);
};

// The delta bases from Prologue's program.
std::array<s32, 8> delta_bases(const sim::Program& program);
// The run's file of an event.
std::string run_path(const std::string& event_label);

} // namespace gt4r::replay::prologue
