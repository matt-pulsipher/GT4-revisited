#pragma once

// Gran Turismo 4 Prologue's converter (0x3C74B8 and its routines, docs/formats/prologue.md "The converter"): a car's spec
// (Gran Turismo 3's layout, 0x3E2 bytes) into its parameter block (0xEDC bytes) as the car's set-up makes it, in the
// PlayStation 2's arithmetic and the program's order of operations, so that the block comes out word for word as the
// program's (tools/gt4diff compares the two on every car: `GT4R_CONVERT=1 gt4r_diff <prologue disc> prologue`).
//
// The block is Gran Turismo 3's (the shared engine's ParamImage, 0xEB0 bytes) with each tire record 4 bytes longer (0x1B8:
// its +0x1B4 .. +0x1B6 the record's kind bytes), what follows the six records 24 bytes on, 20 bytes more after the
// aerodynamics (+0xE84 .. +0x97: an aerodynamic device's switch, speeds, rate and downforce) and so what follows 44 bytes on.

#include "gt4r/base/types.h"
#include "gt4r/sim/car_spec.h"
#include "base/ee_float.h"
#include "sim/exact/params_build.h"

#include <array>
#include <cstring>
#include <string>

namespace gt4r::sim {
class Program;
}

namespace gt4r::sim::prologue {

using gt3r::ee::F;
namespace ex = gt3r::sim::exact;

constexpr u32 kBlockSize = 0xEDC;
using Block = std::array<u8, kBlockSize>;

// What the converter reads of the program besides the spec, as the race's start leaves it.
struct ConvertData {
    std::array<u8, 0x120> statics{};               // 0x503208: the static physics block (Gran Turismo 3's layout)
    std::array<std::array<u8, 0x54>, 4> layouts{}; // 0x503FA8: the wheel layouts, by the parameter block's +0x30
    std::array<std::array<u8, 0x78>, 3> special{}; // 0x5040F8: the special cars' bodies and tires, by the block's +0x33
    std::array<u8, 8> lsd{};                       // 0x504AD0: the differential type letters
    u8 awd3_gain = 0, awd3_cap = 0;                // 0x504AD8 / 0x504AD9: four wheel drive system 3's gain (%) and capacity
    u8 kind7_tire = 0;                             // 0x503CB0: 0 leaves the special kind 7 without the pitch's tire term
    std::array<u8, 18> gear_steps{};               // 0x504AF0: a made gearbox's steps, the rows of 5, 6 and 7 gears (0x3E0BE0)
    F air_density;                                 // 0x504A74 (0x3DBA58: the static block's +0 x 0.1)
    // The rolling resistance's curve (0x504428) and its paved surface's factor (0x504438: the static block's s16 +0x2C x
    // 0.01), as the race's start makes them (0x3C5BC0).
    u8 roll_n = 0;
    std::array<F, 8> roll_x{}, roll_y{};
    F roll_scale;
    // The spec builder's (0x3B3998) defaults: the steering curves' points (0x571C78 x, 0x571C80 y), the surface classes
    // (0x571C88), each tire block's curve B (0x503E18 x, 0x503E20 y).
    std::array<u8, 6> steer_x{}, steer_y{}, curve_b_x{}, curve_b_y{};
    std::array<u8, 16> surface_class{};
};
ConvertData convert_data(const Program& program);

// The spec as Prologue's builder makes it (0x3B3998 over 0x404620; Gran Turismo 3's layout) from ours (Gran Turismo 4's
// builder on the database, `s`) for the car `label` on a course of kind `course_kind` (2: dirt): the core's spec (core_spec)
// with the inner builder's bytes where they differ from ours, then the wrapper's rules (the special cars by label, their
// aerodynamic devices, the overhangs, the pitch inertia's scale, the defaults).
void core_spec(const Spec& s, const std::string& label, int course_kind, const ConvertData& d, ex::Spec& out);

// 0x3E0BE0 (Gran Turismo 4's 0x354FF8): a made gearbox's ratios x 1000 from the spec as the inner builder makes it (the
// core's layout): its gears (+0xC8), rev byte (+0x6F), final drive (+0xC6, x 0.001 in the soft doubles) and the driven
// axle's tire radius (0x3DFFD0's axle, 0x3DFE98), and the slider x 10 (km/h). The top gear's ratio from the speed the
// slider gives at the rev byte's rpm; each gear below by the steps (gear_steps, the row of 5 gears for fewer), eased
// between a row's pairs by (slider x 10 - 150) / 250; first gear once more by the third pair; out[0] (reverse)
// (3 first + second) / 4. out[1 ..] up to the gear count (at most 7).
void gear_ratios(const ex::Spec& spec, const ConvertData& d, int slider10, gt3r::s16 out[8]);

// What the car's set-up (0x3C93A0) hands the converter besides the spec and the block.
struct ConvertIn {
    s32 control = 0;                 // a2: 2, a computer's car (its axles' grips by the rule block)
    std::array<u8, 12> rules{};      // t0: the rule block: +0x03 the power cap (x 10 PS), +0x04 / +0x08 + class the grips (%)
    std::array<u32, 8> condition{};  // t1: the entry's engine condition (+4 / +8 the wear words; zeros: a fresh engine)
};

// 0x3C74B8 on `spec` (which it edits as the program's does) into `p`, built in place over what it holds (the race clears
// it first) at the address `base` (its curve tables hold base + the offset of their points).
void convert(ex::Spec& spec, const ConvertData& d, const ConvertIn& in, u32 base, Block& p);

// A place of the engine's parameter block (Gran Turismo 3's) in Prologue's, and the block in the engine's layout (its curve
// tables' pointers at the engine's offsets from `base`).
u32 block_offset(u32 engine_offset);
ex::ParamImage engine_image(const Block& p, u32 base);

} // namespace gt4r::sim::prologue
