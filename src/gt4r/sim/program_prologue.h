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
    // The surfaces whose steering offset aims at 0 (16 bytes; 0x3B5A20).
    static constexpr u32 kFixedAim = 0x504AC0;
    // The controller settings (Gran Turismo 4's 0x622110 layout; 0x3DEDE0, 0x3DE270) and the pedals' steps a frame (four
    // floats: the throttle's up and down, the brake's up and down; 0x3DE270).
    static constexpr u32 kController = 0x504950, kPedalSteps = 0x504AA8;
    // The in-line wheel layout's roll terms (six floats; the attitude 0x3765F8).
    static constexpr u32 kInLine = 0x50429C;
    // The converter's (0x3C74B8): the special cars' bodies and tires (0x78 bytes a row, three rows; 0x3C1F60), the
    // differential type letters (0x3DF0D8), four wheel drive system 3's gain and capacity bytes (0x3DFFD0), the byte that
    // gives the special kind 7 its pitch tire term (0x3B1498).
    static constexpr u32 kSpecialBodies = 0x5040F8, kLsdLetters = 0x504AD0, kAwd3 = 0x504AD8, kKind7Tire = 0x503CB0;

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
