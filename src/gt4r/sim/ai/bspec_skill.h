#pragma once

// The B-spec driver's skill (0x468C50, from the conductor's virtual 28 at the race's start for a car of control source 0
// or 1, with the switch 0x623E48 set): the driver's record of its races (0x40 bytes the race information carries at +0x20,
// copied to car +0x8C: experience by the cars' class, docs/specs/ai.md "B-spec") weighed around the car's own class into
// the driver's arcs', braking and alongside multipliers (D +0xB8 / +0xBC / +0xC0). The values are the program's
// (its table 0x623E40, the bytes 0x623E48 ..), read from CORE.GT4.

#include "base/ee_float.h"
#include "gt4r/base/types.h"
#include "gt4r/sim/program.h"

#include <array>
#include <optional>

namespace gt4r::sim::ai {

struct BSpecRecord {
    std::array<u16, 8> a{}, b{}, c{}; // +0x00 / +0x10 / +0x20 by class 0 .. 7 (a, b capped at 1024 when read, c at 256)
    std::array<u16, 4> d{}, e{};      // +0x30 / +0x38 by half the class (their high bytes read)
    static BSpecRecord from_bytes(const u8* p); // the record's 0x40 bytes
};

struct BSpecSkill {
    gt3r::ee::F corner, brake, alongside; // D +0xB8 / +0xBC / +0xC0
};

struct BSpecTables {
    bool on = false;                     // 0x623E48
    std::array<u8, 8> weight{};          // 0x623E40: each class's weight
    std::array<gt3r::s8, 6> base{};            // 0x623E4A ..: (base %, range %) for each multiplier
    static BSpecTables load(const Program& program);
};

// The car's class (0x468458 → 0x468418): (rating − 0.35) / 0.55 within [0, 1], x 8, at most 7; `rating` the parameter
// block's +0xFE0 (0x3F86E8 at the car's conversion).
int bspec_class(gt3r::ee::F rating);
// `course_flag`: the race's byte cond + 0xCBE7 (0x468A40: set, the +0x30 / +0x38 records are read) ❓ what it is.
// nullopt with the switch off (the multipliers stay as they are).
std::optional<BSpecSkill> bspec_skill(const BSpecTables& t, const BSpecRecord& r, gt3r::ee::F rating, bool course_flag);

} // namespace gt4r::sim::ai
