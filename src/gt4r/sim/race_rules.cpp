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

constexpr u32 kV1 = 3;
// Gran Turismo 4's penalty's speed cap (km/h): the constant 0x35E108 loads at 0x35E2B4 (SCUS-97328 v1.01; the disc test
// race_rules_penalty_cap_is_the_program's reads it from the program's code).
constexpr u8 kPenaltyCap = 50;

// 0x3E1C20's case at `at`: the bytes and halfwords it stores on the rule block ($t0) from constants, up to its return (a few
// instructions: constants into registers, stores, a jump over the next case).
void run_case(const Program& p, u32 at, std::array<u8, 0x80>& block) {
    std::array<s32, 32> reg{};
    const auto step = [&](u32 w) {
        const u32 op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31;
        const s16 imm = s16(w);
        if (op == 0x09 && rs == 0) reg[rt] = imm; // addiu $rt, $zero, imm
        else if ((op == 0x28 || op == 0x29) && rs == 8 && imm >= 0 && size_t(imm) + 1 < block.size()) { // sb / sh $rt, imm($t0)
            block[size_t(imm)] = u8(reg[rt]);
            if (op == 0x29) block[size_t(imm) + 1] = u8(reg[rt] >> 8);
        }
    };
    for (int n = 0; n < 64; ++n) {
        const u32 w = p.w(at);
        if (w == 0x03E00008) { // jr $ra, its delay slot first
            step(p.w(at + 4));
            return;
        }
        if ((w >> 16) == 0x1000) { // b
            step(p.w(at + 4));
            at = at + 4 + u32(s32(s16(w)) * 4);
            continue;
        }
        step(w);
        at += 4;
    }
}

} // namespace

RaceRules race_rules(ByteSpan row, const Program& program) {
    RaceRules r;
    if (row.size() < 0x8C) return r;
    r.hits.pylons = row[0x75], r.hits.car_limit = row[0x8B], r.hits.wall_limit = row[0x8A], r.hits.penalty = row[0x88];
    if (program.known()) {
        r.penalty_cap = kPenaltyCap;
    } else if (PrologueProgram::locate(program).recognized()) {
        r.penalty_cap = u8(constant(program, 0x3C5B48, kV1));
        // The test's case of the switch (0x572CC0, tests 1 .. 36).
        const u32 test = row[0x72];
        r.start_speed[0] = row[0x42], r.start_distance[0] = s16(row[0x14] | row[0x15] << 8);
        if (test >= 1 && test <= 36) {
            std::array<u8, 0x80> block{};
            run_case(program, program.w(0x572CC0 + 4 * (test - 1)), block);
            r.speed_cap[0] = block[0x42], r.mileage = block[0x45];
            for (size_t k = 1; k < 6; ++k) r.start_speed[k] = block[0x4E + k], r.start_distance[k] = s16(block[0x54 + 2 * k] | block[0x55 + 2 * k] << 8);
        }
        for (size_t k = 1; k < 6; ++k)
            if (r.start_speed[k] == 0) r.start_speed[k] = r.start_speed[0]; // (DynamicsConductorTraining's virtual 33)
    }
    return r;
}

} // namespace gt4r::sim
