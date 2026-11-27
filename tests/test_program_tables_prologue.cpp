// Gran Turismo 4 Prologue's program tables as values (sim/program_tables_prologue.h) and the disc's program checked against
// them: the areas byte for byte, the license tests' rules decoded from the program's own code at test time.

#include "test_framework.h"

#include "gt4r/sim/program.h"
#include "gt4r/sim/program_prologue.h"
#include "gt4r/sim/program_tables_prologue.h"

#include <cstdio>

using namespace gt4r;

namespace {

Disc* the_prologue_disc() {
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::Prologue) return d;
    return nullptr;
}

// The word at a program address of the disc's image.
u32 word(const sim::Program& p, u32 a) {
    const ByteSpan im = p.image();
    const u32 o = a - p.base();
    return o + 4 <= im.size() ? u32(im[o]) | u32(im[o + 1]) << 8 | u32(im[o + 2]) << 16 | u32(im[o + 3]) << 24 : 0;
}

// 0x3E1C20's case at `at` as the program runs it: the constants its instructions load and store on the rule block ($t0) up to
// its return (decoded at test time from the disc; nothing of it is kept).
std::array<u8, 0x80> run_case(const sim::Program& p, u32 at) {
    std::array<u8, 0x80> block{};
    std::array<s32, 32> reg{};
    const auto step = [&](u32 w) {
        const u32 op = w >> 26, rs = (w >> 21) & 31, rt = (w >> 16) & 31;
        const gt3r::s16 imm = gt3r::s16(w);
        if (op == 0x09 && rs == 0) reg[rt] = imm;
        else if ((op == 0x28 || op == 0x29) && rs == 8 && imm >= 0 && size_t(imm) + 1 < block.size()) {
            block[size_t(imm)] = u8(reg[rt]);
            if (op == 0x29) block[size_t(imm) + 1] = u8(reg[rt] >> 8);
        }
    };
    for (int n = 0; n < 64; ++n) {
        const u32 w = word(p, at);
        if (w == 0x03E00008) return step(word(p, at + 4)), block;
        if ((w >> 16) == 0x1000) {
            step(word(p, at + 4));
            at = at + 4 + u32(s32(gt3r::s16(w)) * 4);
            continue;
        }
        step(w);
        at += 4;
    }
    return block;
}

} // namespace

// The areas: in order, none overlapping, every byte reached by its address, pinned by their hash (a change to a value is a
// change of this hash, made on purpose).
TEST(prologue_tables_values) {
    const auto t = sim::prologue_tables();
    size_t bytes = 0;
    for (size_t k = 0; k < t.size(); ++k) {
        bytes += t[k].bytes.size();
        if (k) CHECK(t[k - 1].address + t[k - 1].bytes.size() <= t[k].address);
        CHECK(sim::prologue_table_at(t[k].address) == &t[k]);
        CHECK(sim::prologue_table_at(u32(t[k].address + t[k].bytes.size() - 1)) == &t[k]);
    }
    CHECK(sim::prologue_table_at(0x503207) == nullptr);
    std::printf("  %zu areas, %zu bytes, hash 0x%016llx\n", t.size(), bytes, (unsigned long long)sim::prologue_tables_hash());
    CHECK(sim::prologue_tables_hash() == 0x540bd5ecd1c8fb99ull);
    CHECK(sim::prologue_lesson_rules()[0].speed_cap == 50 && sim::prologue_lesson_rules()[35].mileage == 100);
}

// The disc's program: every area equal to ours at its place, the program served ours, and the license tests' rules and the
// penalty's cap as the program's code makes them.
TEST(disc_prologue_tables_equal_the_disc) {
    Disc* disc = the_prologue_disc();
    if (!disc) SKIP("no Prologue disc");
    const sim::Program p = sim::Program::load(*disc);
    CHECK(p.prologue_values());
    CHECK(sim::PrologueProgram::locate(p).recognized());
    const sim::TableCheck c = sim::check_prologue_tables(p.image(), p.base(), true);
    for (const auto& r : c.rows) {
        if (r.differing || !r.found) std::printf("  %s (0x%06X): %s, %u bytes differ\n", r.table->what, r.table->address, r.how, r.differing);
        CHECK(r.found && r.differing == 0);
    }
    std::printf("  %zu areas equal to the disc's\n", c.rows.size());
    for (const sim::ProgramTable& t : sim::prologue_tables())
        for (u32 k = 0; k < t.bytes.size(); ++k) CHECK(p.b(t.address + k) == t.bytes[k]);
    // The penalty's cap: the constant `addiu $v1, $zero, 50` at 0x3C5B48.
    const u32 w = word(p, 0x3C5B48);
    CHECK((w >> 16) == 0x2403 && (w & 0xFFFF) == sim::kProloguePenaltyCap);
    int equal = 0;
    for (u32 test = 1; test <= 36; ++test) {
        const std::array<u8, 0x80> b = run_case(p, word(p, 0x572CC0 + 4 * (test - 1)));
        const sim::PrologueLessonRule& k = sim::prologue_lesson_rules()[test - 1];
        bool same = b[0x42] == k.speed_cap && b[0x45] == k.mileage;
        for (size_t j = 1; j < 6; ++j)
            same = same && b[0x4E + j] == k.start_speed[j - 1] && gt3r::s16(b[0x54 + 2 * j] | b[0x55 + 2 * j] << 8) == k.start_distance[j - 1];
        equal += same;
        if (!same) std::printf("  test %u's rules differ\n", test);
    }
    std::printf("  %d of 36 license tests' rules as the code makes them\n", equal);
    CHECK(equal == 36);
}
