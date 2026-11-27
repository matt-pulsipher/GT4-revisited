#pragma once

// The car's packed spec (0x600 bytes) as the program builds it from a race entry's equipment: one
// helper per parts table, then the fix-ups (docs/specs/physics.md "The spec"). The converter turns it
// into the car's parameter block; here it is also what the menus' figures come from.

#include "gt4r/sim/equipment.h"
#include "gt4r/sim/program.h"

#include <array>

namespace gt4r::sim {

struct Spec {
    static constexpr u32 kSize = 0x600;
    std::array<u8, kSize> bytes{};
    u8 b(u32 o) const { return bytes[o]; }
    gt3r::s8 sb(u32 o) const { return gt3r::s8(bytes[o]); }
    u16 h(u32 o) const { return u16(bytes[o] | (bytes[o + 1] << 8)); }
    s16 sh(u32 o) const { return s16(h(o)); }
    void set_b(u32 o, u32 v) { bytes[o] = u8(v); }
    void set_h(u32 o, u32 v) { bytes[o] = u8(v), bytes[o + 1] = u8(v >> 8); }

    // Fields by meaning (the offsets the specification names).
    int weight_kg() const { return sh(0x20); }
    int gears() const { return b(0x155); }
    int drive() const { return b(0x128); }
    // The tire blocks: front 0 .. 2, rear 3 .. 5 (0xA8 bytes each).
    static constexpr u32 kTireBlock = 0xA8;
    static constexpr u32 tire_block(int k) { return k < 3 ? 0x210 + kTireBlock * u32(k) : 0x408 + kTireBlock * u32(k - 3); }
};

// 0x3F3B68: the spec of an equipment. `course_kind`: the course's kind as the race gives it (2 and 3:
// loose surfaces, where a rally car's suspension is raised). Rows the database lacks read as zeros.
// The program's defaults come from `program` (zeros when it is not a known build).
void build_spec(db::SpecDb& db, const Program& program, const Equipment& eq, int course_kind, Spec& out);

// The entry's aid switches as the car's set-up applies them before the converter (0x363CB8 .. 0x363E0C): `asm_on` and
// `tcs_on` (the entry's +0x3450 / +0x3454; both off on a loose course) and the car's own have: off takes the spec's ASM
// channels (+0x1B8; 0x3F1700) or its TCS (+0x1C2; 0x3F1748); returns the record's aid bits F +0x4E5 of a car of control
// source 0, 1 or 3 (a computer's car has none): 1 TCS, 4 ASM, 2 the cornering throttle hold (the program's byte 0x620E09:
// 1 with TCS, 2 with ASM, 3 always), 8 `wheel_aid` (the entry's +0x3458: a steering wheel's slip aid), 0x10 the pad's slip
// aid (the byte 0x620E0B: 1 with ASM, 2 always).
u8 aid_switches(Spec& s, const Program& program, bool asm_on, bool tcs_on, bool wheel_aid, bool loose);

// 0x443240: a computed gearbox's ratios from its slider into the equipment's gear words (Gran Turismo 4's
// generator 0x354FF8; on Prologue its 0x406190 and 0x3E0BE0). False (nothing done) for a gearbox with stored
// ratios, more than 7 gears, or a program that is neither's.
bool computed_gears(db::SpecDb& db, const Program& program, Equipment& eq);

// A car as sold with its settings and a computed gearbox's ratios: stock_equipment, then computed_gears.
bool stock_car(db::SpecDb& db, const Program& program, const std::string& car_label, Equipment& out);

// The menus' figures of a spec: the engine's peak power and torque with every part's factor.
struct SpecFigures {
    float ps = 0, kgfm = 0;
    int power_rpm = 0, torque_rpm = 0;
    int rev_limit_rpm = 0;
    float tire_radius[2] = {}; // metres
};
SpecFigures spec_figures(const Spec& s);

} // namespace gt4r::sim
