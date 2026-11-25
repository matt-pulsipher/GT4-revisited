// gt4r_diff: the B-spec driver's book (showroom_check.h, menu/game_bspec.h) against the user's own program.
// Routines are named by address; nothing of the program is kept.

#include "showroom_check.h"

#include "orig.h"

#include "gt4r/menu/game_bspec.h"
#include "gt4r/sim/ai/bspec_skill.h"
#include "gt4r/sim/program.h"

#include <cstdio>
#include <random>

namespace gt4r::diff {

BookResult run_bspec_book(Disc& disc, bool print, int books) {
    BookResult r;
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) return r;
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    const sim::ai::BSpecTables tables = sim::ai::BSpecTables::load(program);
    const menu::bspec::Weights weights = tables.weight;
    // The game object: only its book (+0x3E4) is read; the course record's lookup (0x433C10 on +0x14A30) and the
    // label's copy (0x468690) are answered here, the course's marks from a buffer of ours.
    const u32 status = ee.alloc(0x20000), course = ee.alloc(0x238), out = ee.alloc(0x40), label = ee.alloc_cstr("tsukuba");
    ee.w32(0x622F4C, status);
    bool with_course = false;
    ee.hle(0x468690, [](Ee& e) { e.set_v0(e.a(0)); }, "course label");
    ee.hle(0x433C10, [&](Ee& e) { e.set_v0(with_course ? course : 0); }, "course record");
    std::mt19937 rng(11);
    auto value = [&rng](u32 most) { return rng() % 3 == 0 ? rng() % 0x10000 : rng() % most; };
    for (int n = 0; n < books; ++n) {
        menu::bspec::Book book{};
        for (u16& v : book) v = u16(n == 0 ? 0 : value(n % 2 ? 0x500 : 0x140));
        for (size_t i = 0; i < book.size(); ++i) ee.w16(status + 0x3E4 + 2 * u32(i), book[i]);
        // The four figures.
        const u32 sp = ee.alloc(16);
        const s32 total = ee.call(0x468B58, {Arg::ptr(sp), Arg::ptr(sp + 4), Arg::ptr(sp + 8)}).v0_s32();
        const s32 theirs[4] = {total, s32(ee.r32(sp)), s32(ee.r32(sp + 4)), s32(ee.r32(sp + 8))};
        const int ours[4] = {menu::bspec::total_experience(book, weights), menu::bspec::car_experience(book, weights),
                             menu::bspec::course_experience(book, weights), menu::bspec::battle_experience(book, weights)};
        for (int i = 0; i < 4; ++i) {
            ++r.figures;
            if (theirs[i] == ours[i]) ++r.figures_equal;
            else if (print) std::printf("  book %d figure %d: the program's %d, ours %d\n", n, i, theirs[i], ours[i]);
        }
        for (int k = 0; k < 8; ++k) {
            const float rating = 0.35f + 0.55f * (float(k) + 0.5f) / 8.0f;
            u8 marks[8];
            for (u8& m : marks) m = u8(rng());
            with_course = (n + k) % 2 == 1;
            for (u32 i = 0; i < 8; ++i) ee.w8(course + 0x228 + i, marks[i]);
            // The race's record.
            for (u32 i = 0; i < 0x40; ++i) ee.w8(out + i, 0xEE);
            ee.call(0x468710, {Arg::ptr(out), Arg::ptr(label), Arg::flt(rating)});
            const std::array<u8, 0x40> mine = menu::bspec::race_record(book, k, with_course ? marks : nullptr);
            bool same = true;
            for (u32 i = 0; i < 0x40; ++i) same = same && ee.r8(out + i) == mine[i];
            ++r.records;
            if (same) ++r.records_equal;
            else if (print && r.records - r.records_equal <= 5) std::printf("  book %d class %d: the race's record differs\n", n, k);
            // A record after a race, back into the book.
            std::array<u8, 0x40> after{};
            for (u32 i = 0; i < 0x40; i += 2) {
                const u32 v = value(0x500);
                after[i] = u8(v), after[i + 1] = u8(v >> 8);
            }
            for (u32 i = 0; i < 0x40; ++i) ee.w8(out + i, after[i]);
            const bool doubled = (n + 2 * k) % 3 != 0;
            const int flag = with_course ? int(rng() % 2) : -1;
            ee.call(0x468860, {Arg::ptr(out), Arg::ptr(label), Arg::integer(flag), Arg::integer(doubled), Arg::flt(rating)});
            menu::bspec::Book back = book;
            u8 mine_marks[8];
            std::copy(marks, marks + 8, mine_marks);
            menu::bspec::write_back(back, k, after, doubled, flag < 0 ? 0 : flag == 0 ? 1 : 2, flag < 0 ? nullptr : mine_marks);
            bool equal = true;
            for (size_t i = 0; i < back.size(); ++i) equal = equal && ee.r16(status + 0x3E4 + 2 * u32(i)) == back[i];
            // (Only the sets the routine wrote: it copies all eight bytes back from a buffer it filled in part.)
            for (u32 i = 0; i < u32(flag < 0 ? 0 : flag == 0 ? 4 : 8); ++i) equal = equal && ee.r8(course + 0x228 + i) == mine_marks[i];
            ++r.backs;
            if (equal) ++r.backs_equal;
            else if (print && r.backs - r.backs_equal <= 5) std::printf("  book %d class %d: the book after the race differs\n", n, k);
            for (size_t i = 0; i < book.size(); ++i) ee.w16(status + 0x3E4 + 2 * u32(i), book[i]);
        }
    }
    if (print)
        std::printf("bspec book: figures %zu of %zu equal, race records %zu of %zu, written back %zu of %zu\n", r.figures_equal, r.figures, r.records_equal,
                    r.records, r.backs_equal, r.backs);
    r.ran = true;
    return r;
}

} // namespace gt4r::diff
