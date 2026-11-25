// A race's rules of hits and speed caps (race_rules.h).

#include "gt4r/sim/race_rules.h"

#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"

namespace gt4r::sim {

namespace {

// The constant an `addiu $reg, $zero, imm` at `address` loads (0: another instruction there).
int constant(const Program& p, u32 address, u32 reg) {
    const u32 w = p.w(address);
    return (w >> 16) == (0x2400u | reg) ? int(w & 0xFFFF) : 0;
}

constexpr u32 kV0 = 2, kV1 = 3, kA0 = 4;

} // namespace

RaceRules race_rules(ByteSpan row, const Program& program) {
    RaceRules r;
    if (row.size() < 0x8C) return r;
    r.hits.pylons = row[0x75], r.hits.car_limit = row[0x8B], r.hits.wall_limit = row[0x8A], r.hits.penalty = row[0x88];
    if (program.known()) {
        r.penalty_cap = u8(constant(program, 0x35E2B4, kA0));
    } else if (PrologueProgram::locate(program).recognized()) {
        r.penalty_cap = u8(constant(program, 0x3C5B48, kV1));
        // The test's entry of the switch (0x572CC0, tests 1 .. 36): one that stores a constant's byte at +0x42 and ends.
        const u32 test = row[0x72];
        if (test >= 1 && test <= 36) {
            const u32 at = program.w(0x572CC0 + 4 * (test - 1));
            const int v = constant(program, at, kV0);
            if (v > 0 && program.w(at + 4) == 0x03E00008 && program.w(at + 8) == 0xA1020042) r.speed_cap[0] = u8(v);
        }
    }
    return r;
}

} // namespace gt4r::sim
