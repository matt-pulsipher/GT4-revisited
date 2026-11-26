// Gran Turismo 4's program tables as values (sim/program_tables.h) and the disc's program checked against them.

#include "test_framework.h"

#include "base/bytes.h"
#include "disc/core_image.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_tables.h"

#include <cstdio>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4) return d.get();
    return nullptr;
}

} // namespace

// The tables as values: in order, none overlapping, every byte reached by its address, and pinned by their hash (a change
// to a value is a change of this hash, made on purpose).
TEST(program_tables_values) {
    const std::span<const sim::ProgramTable> t = sim::gt4_tables();
    CHECK(t.size() == 14);
    size_t bytes = 0;
    for (size_t k = 0; k < t.size(); ++k) {
        bytes += t[k].bytes.size();
        if (k) CHECK(t[k - 1].address + t[k - 1].bytes.size() <= t[k].address);
        CHECK(sim::gt4_table_at(t[k].address) == &t[k] && sim::gt4_table_at(t[k].address + u32(t[k].bytes.size()) - 1) == &t[k]);
        CHECK(sim::gt4_table_at(t[k].address - 1) != &t[k] && sim::gt4_table_at(t[k].address + u32(t[k].bytes.size())) != &t[k]);
    }
    CHECK(bytes == 6084);
    std::printf("  %zu tables, %zu bytes, hash 0x%016llx\n", t.size(), bytes, (unsigned long long)sim::gt4_tables_hash());
    CHECK(sim::gt4_tables_hash() == 0xe50e2960529eadf5ull);
    // A few values as the readers know them: the static block's rolling-resistance points, the pedals' ramps (1/6), a soft
    // material (2: gravel).
    auto byte = [](u32 a) { const sim::ProgramTable* t = sim::gt4_table_at(a); return t ? t->bytes[a - t->address] : u8(0); };
    auto word = [&](u32 a) { return u32(byte(a)) | u32(byte(a + 1)) << 8 | u32(byte(a + 2)) << 16 | u32(byte(a + 3)) << 24; };
    CHECK(byte(sim::Program::kStatic + 0x0B) == 8);
    CHECK(word(0x620270) == 0x3E2AAAAA);
    CHECK(word(sim::Program::kSoft + 4 * 2) != 0 && word(sim::Program::kSoft) == 0);
}

// The disc's program against ours: SCUS-97328 v1.01's every table bit for bit; a program changed in memory still races on
// ours, its differences logged.
TEST(disc_program_tables_equal_the_disc) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const sim::Program p = sim::Program::load(*disc);
    CHECK(p.known() && p.ours());
    const sim::TableCheck& c = p.check();
    for (const std::string& line : c.lines()) std::printf("  %s\n", line.c_str());
    CHECK(c.reference && c.rows.size() == sim::gt4_tables().size() && c.differ() == 0 && c.not_found() == 0);
    for (const sim::TableCheck::Row& r : c.rows) CHECK(r.found == r.table->address);
    // Every byte served is the disc's.
    const std::shared_ptr<const gt3r::CoreImage> core = disc->program_image();
    for (const sim::ProgramTable& t : sim::gt4_tables())
        for (u32 k = 0; k < t.bytes.size(); ++k) CHECK(p.b(t.address + k) == core->program[t.address + k - core->load_address]);
    // A copy with the static block changed: ours still serve; the check names the table.
    auto changed = std::make_shared<gt3r::CoreImage>(*core);
    changed->program[sim::Program::kStatic + 0x34 - changed->load_address] ^= 0xFF;
    const sim::Program q = sim::Program::from_core(changed);
    CHECK(q.known() && q.b(sim::Program::kStatic + 0x34) == p.b(sim::Program::kStatic + 0x34));
    CHECK(q.check().differ() == 1);
    for (const sim::TableCheck::Row& r : q.check().rows)
        if (r.differing) CHECK(r.table->address == 0x620AD8 && r.differing == 1 && r.first_differing == sim::Program::kStatic + 0x34 - 0x620AD8);
    // The interpreter's image (from_image) serves its own bytes.
    const sim::Program own = sim::Program::from_image(Bytes(changed->program), changed->load_address);
    CHECK(own.known() && !own.ours() && own.b(sim::Program::kStatic + 0x34) == u8(p.b(sim::Program::kStatic + 0x34) ^ 0xFF));
}

// Another build's program: the tables found by the routines that address them. On v1.01 every one at its place; in a copy
// with the code moved and the data moved and every address to the data rebuilt (as a build with more code before them
// would be), every one at its new place, equal.
TEST(disc_program_tables_found_by_their_routines) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    const std::shared_ptr<const gt3r::CoreImage> core = disc->program_image();
    const u32 load = core->load_address;
    const sim::TableCheck here = sim::check_tables(core->program, load, true);
    for (const std::string& line : here.lines()) std::printf("  v1.01: %s\n", line.c_str());
    CHECK(here.reference && here.not_found() == 0 && here.differ() == 0);
    for (const sim::TableCheck::Row& r : here.rows) CHECK(r.found == r.table->address && r.routine != 0);

    // The copy: the code and what follows it up to 0x620000 moved 0x40 bytes on, the data from 0x620000 1 MB on, and every
    // upper half of an address into the data (0x62 .. 0x6E) raised to match.
    constexpr u32 kCodeShift = 0x40, kDataStart = 0x620000, kDataShift = 0x100000;
    const ByteSpan old = core->program;
    Bytes moved(old.size() + kDataShift, 0);
    std::copy(old.begin(), old.begin() + (kDataStart - load), moved.begin() + kCodeShift);
    std::copy(old.begin() + (kDataStart - load), old.end(), moved.begin() + (kDataStart + kDataShift - load));
    for (u32 o = kCodeShift; o < kDataStart - load + kCodeShift; o += 4) {
        const u32 w = gt3r::rd32(moved, o);
        if ((w >> 26) == 0x0F && (w & 0xFFFF) >= 0x62 && (w & 0xFFFF) <= 0x6E) {
            const u32 v = w + (kDataShift >> 16);
            for (int k = 0; k < 4; ++k) moved[o + u32(k)] = u8(v >> (8 * k));
        }
    }
    const sim::TableCheck there = sim::check_tables(moved, load, false);
    for (const std::string& line : there.lines()) std::printf("  moved: %s\n", line.c_str());
    CHECK(!there.reference && there.not_found() == 0 && there.differ() == 0);
    for (size_t k = 0; k < there.rows.size(); ++k) {
        CHECK(there.rows[k].found == there.rows[k].table->address + kDataShift);
        CHECK(there.rows[k].routine == here.rows[k].routine + kCodeShift);
    }
    // Ours serve a disc of that program, its check made once.
    auto image = std::make_shared<gt3r::CoreImage>(*core);
    image->program = moved;
    const sim::Program p = sim::Program::from_core(image), q = sim::Program::from_core(image);
    CHECK(p.known() && p.ours() && &p.check() == &q.check() && p.check().not_found() == 0 && p.b(sim::Program::kStatic + 0x0B) == 8);
}

