#pragma once

// Gran Turismo 4 Prologue's program (CORE.GT4 of SCPS-15055 v1.01): its build recognized by its code,
// and its own tables of what the shared engine's race core reads. Its car spec is Gran Turismo 3's
// layout, built from the database's rows by a builder (0x404620) with a helper per part as Gran
// Turismo 3's; its physics tables are Gran Turismo 3's tables with Prologue's values. The shared
// engine's locator (sim/exact/program_data.h) does not find them (another compiler's code): they
// are at fixed places in the recognized build, each checked against the routines that address it
// (docs/formats/prologue.md "The program").

#include "gt4r/base/types.h"
#include "sim/exact/program_data.h"

#include <vector>

namespace gt4r::sim {

class Program;

class PrologueProgram {
public:
    struct Place {
        const char* what;
        u32 address = 0; // 0: not found
        const char* how; // "its code" / "not found"
    };
    // A table of the core's (ProgramData::Table) in Prologue v1.01: `size` bytes of the core's
    // table from `offset` at `address`.
    struct TablePlace {
        gt3r::sim::exact::ProgramData::Table table;
        u32 offset, size, address;
        const char* addressed_by; // the routine that addresses it (Prologue's address, for rechecking)
    };
    static const std::vector<TablePlace>& tables();
    // Its own tables of the race step that the core's ProgramData has no place for (rules_prologue.h), each checked against
    // the routine that reads it: the static physics block (Gran Turismo 3's layout, 0x120 bytes; 0x3BA070, 0x3BB138), the
    // camber force's reference angle and gain (0x3BB138), the mean slip angle's switch (0x3BB138), the wheel layouts (0x54
    // bytes a row, Gran Turismo 4's rows; 0x3C0D80 by the parameter block's +0x30).
    static constexpr u32 kStatic = 0x503208, kCamberRef = 0x504294, kCamberGain = 0x504298, kMeanSlipOn = 0x506612;
    static constexpr u32 kWheelLayouts = 0x503FA8;

    // Finds the build's routines in a program image loaded at `load_address`.
    static PrologueProgram locate(ByteSpan image, u32 load_address);
    static PrologueProgram locate(const Program& program);
    // Every routine found where Prologue v1.01 has it.
    bool recognized() const { return recognized_; }
    const std::vector<Place>& places() const { return places_; }
    // Writes Prologue's tables over the core's (GT3's) in `d`; false (and `d` untouched) when the
    // build is not recognized.
    bool apply(const Program& program, gt3r::sim::exact::ProgramData& d) const;

private:
    bool recognized_ = false;
    std::vector<Place> places_;
};

} // namespace gt4r::sim
