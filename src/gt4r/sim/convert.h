#pragma once

// Gran Turismo 4's converter (0x361908 and its routines, docs/specs/physics.md §4.3): a car's spec into its
// parameter block (0x1100 bytes) as the car's set-up makes it, in the PlayStation 2's arithmetic and the
// program's order of operations, so the block comes out byte for byte as the program's (tools/gt4diff
// compares the two on every car: `gt4r_diff <disc> params`). The step's records are then read out of the block
// (convert_record): what the shared engine's mirror of the record has a place for, and Gran Turismo 4's own
// part (record.h).

#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_step.h"

#include <array>
#include <cstring>

namespace gt4r::sim {

struct ParamBlock {
    static constexpr u32 kSize = 0x1100;
    std::array<u8, kSize> bytes{};
    // The block's address: its tables (a count and two pointers) hold base + the offset of their points.
    u32 base = 0;

    gt3r::ee::F f(u32 o) const { return gt3r::ee::F::raw(w(o)); }
    void set_f(u32 o, gt3r::ee::F v) { set_w(o, v.bits); }
    u8 b(u32 o) const { return bytes[o]; }
    void set_b(u32 o, u32 v) { bytes[o] = u8(v); }
    u16 h(u32 o) const { return u16(bytes[o] | (bytes[o + 1] << 8)); }
    s16 sh(u32 o) const { return s16(h(o)); }
    void set_h(u32 o, u32 v) { bytes[o] = u8(v), bytes[o + 1] = u8(v >> 8); }
    u32 w(u32 o) const {
        u32 v;
        std::memcpy(&v, &bytes[o], 4);
        return v;
    }
    void set_w(u32 o, u32 v) { std::memcpy(&bytes[o], &v, 4); }
};

// What the car's set-up (0x363B10) gives the converter besides the spec.
struct ConvertIn {
    // a2: the car's control source (F +0x466). With 2 (a computer drives the car) the axles' grips take the rule
    // block's bytes for the car's drive class.
    s32 control = 0;
    // t0: the conductor's rule block (conductor + 0xF818): +0x03 the power cap's rule (x 10 PS; 0: none), +0x04 + class
    // and +0x08 + class the axles' grips for a computer's car (percent; 0 is 100).
    std::array<u8, 0x80> rules{};
    // t1: the entry's engine condition block (entry + 0x3468, 0x20 bytes): +0x04 / +0x08 two wear words and +0x14 a byte
    // that, set, means a fresh engine. Null: a fresh engine (the program's 0x3433E8 default).
    const u8* condition = nullptr;
};

// What the converter leaves in the program's data besides the block, which the race's step reads.
struct ConvertGlobals {
    // 0x620CB0 .. 0x620CD4 (0x360A78): the dampers' square-law table: on (the static block's byte +0x102), each axle's
    // bump and rebound knees and shares.
    u32 damper_on = 0;
    std::array<gt3r::ee::F, 2> damper_bump_knee{}, damper_bump_share{}, damper_rebound_knee{}, damper_rebound_share{};
    // 0x62030C / 0x620314 (0x34BC98, a car with four wheel drive system 5 only): that system's fade start and floor.
    bool awd_fade_set = false;
    gt3r::ee::F awd_fade_start, awd_fade_floor;
};

// 0x361908 on our spec (a copy: the converter changes a few of its bytes as the program's does, e.g. a drive above 4
// reads as 0, the tire blocks' curves' first points). `race`: the race's tables the converter reads (the air's density
// 0x622308, the rolling resistance's curve 0x620938 and its paved surface's factor 0x620984), as the race's start makes
// them. `p` is built in place over what it holds (the car's set-up clears it first: a fresh block).
void convert(Spec spec, const Program& program, const rules::Tables& race, const ConvertIn& in, ParamBlock& p, ConvertGlobals* globals = nullptr);

// 0x350678: the engine condition's limits (the program's words 0x620320 .. 0x620340, from the static block's bytes +0xD0 ..
// +0xD8): l[0] the first wear word's least, l[1] the torque lost there, l[2] / l[3] the span past which it falls by l[4]; the
// second word's l[5] (negative) and l[6], l[7] its greatest, l[8] where its fall starts.
struct ConditionTable {
    gt3r::ee::F l[9];
};
ConditionTable condition_table(const Program& program);

// 0x35A058: a special car (the layout row 1, or the car category 6), whose body and tires come from the program's
// table 0x6205C8 (0x78 bytes a row, by the spec's byte +0x23).
bool special_car(const Spec& s);

// The step's record of a car from its block: every member of the engine's mirror and of Gran Turismo 4's own part
// that has a place in the parameter block (car_layout.h, record.h), and the wheel layout's row; then the mirror's members
// the block keeps in another shape, made from Gran Turismo 4's (its view for the engine's routines that still run: the
// engine curve thinned to 16 points, 7 forward gears at most, the tire records' 24 surfaces folded to 16 rows, the
// steering and camber curves, the inverse masses).
void load_params(const ParamBlock& p, const Program& program, Record& r);

// A car's record as its set-up makes it: the block (convert, at a made-up address), read into r.car.p and r.x
// (load_params), and the masses the record starts with. `block`: the block too, when wanted.
void build_record(const Spec& s, const Program& program, const rules::Tables& race, const ConvertIn& in, Record& r, ParamBlock* block = nullptr,
                  ConvertGlobals* globals = nullptr);

} // namespace gt4r::sim
