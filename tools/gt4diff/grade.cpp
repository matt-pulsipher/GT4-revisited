// gt4r_diff: the race picture's correction (docs/formats/strobe.md "The pause as the program drives it"): the program's
// record-to-correction routine (0x3EFDF0 -> 0x3EFC10) and its table of levels (0x4A8CB8) in the interpreter, against
// ours (race/picture_settings.h) for every screen setting. Routines are named by address; nothing of the program is kept.

#include "orig.h"

#include "gt4r/race/picture_settings.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/program_tables.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <random>

namespace gt4r::diff {

namespace {

u32 bits_of(float f) {
    u32 b;
    std::memcpy(&b, &f, 4);
    return b;
}

// The program's correction for a record of four settings: {gamma r, g, b, black, white, saturation} (0x3EFDF0).
std::array<u32, 6> program_correction(Ee& ee, const std::array<int, 4>& v) {
    const u32 rec = ee.alloc(0x10, 16), out = ee.alloc(0x20, 16);
    for (u32 k = 0; k < 4; ++k) ee.w32(rec + 4 * k, u32(v[k]));
    ee.call(0x3EFDF0, {Arg::ptr(out), Arg::ptr(rec)});
    std::array<u32, 6> c{};
    for (u32 k = 0; k < 6; ++k) c[k] = ee.r32(out + 4 * k);
    return c;
}

// The program's 256 levels for a correction (0x4A8CB8: f12-f14 the gammas, f15 black, f16 white, f17 the most), read where it
// stores each entry's three bytes.
std::array<std::array<u8, 3>, 256> program_levels(Ee& ee, const std::array<u32, 6>& c, float most) {
    std::array<std::array<u8, 3>, 256> out{};
    const u32 packet = ee.alloc(0x2000, 16);
    ee.w32(Ee::kScratchBase + 0x2000 + 0x1F8, packet); // the packet's write pointer (0x4ABC90)
    ee.w64(Ee::kScratchBase + 0x2000 + 0x2C0, 0);       // no open packet (0x49FE28)
    int n = 0;
    ee.probe(0x4A8F1C, [&](Ee& e) {
        // Entry n is stored for the input level n with bits 3 and 4 swapped (the CLUT's storage order): the GS reads level i
        // at the swapped place, so the table's level for an input i is the one stored at swap(i).
        const int i = (n & 0xE7) | ((n & 8) << 1) | ((n & 0x10) >> 1);
        if (n < 256) out[size_t(i)] = {u8(e.gpr[7].lo), u8(e.gpr[6].lo), u8(e.gpr[5].lo)};
        ++n;
    });
    ee.call(0x4A8CB8, {Arg::ptr(packet + 0x1000), Arg::fbits(c[0]), Arg::fbits(c[1]), Arg::fbits(c[2]), Arg::fbits(c[3]),
                       Arg::fbits(c[4]), Arg::flt(most)});
    ee.clear_probes();
    return out;
}

} // namespace

bool run_grade(Disc& disc) {
    const sim::Program prog = sim::Program::load(disc);
    if (!prog.known() || prog.prologue_values() || prog.release() != sim::Release::Us) {
        std::printf("not the mapped Gran Turismo 4 program (US)\n");
        return false;
    }
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc, sdb);
    int records = 0, records_equal = 0, tables = 0, tables_equal = 0, levels_off = 0;
    for (int b = -5; b <= 5; ++b)
        for (int k = -5; k <= 5; ++k)
            for (int s = -5; s <= 5; s += 5)
                for (int t = -5; t <= 5; ++t) {
                    const std::array<int, 4> v = {b, k, s, t};
                    const std::array<u32, 6> pc = program_correction(ee, v);
                    race::PictureSettings ps;
                    ps.v = v;
                    const race::PictureSettings::Correction oc = ps.correction();
                    const std::array<u32, 6> ours = {bits_of(oc.gamma[0]), bits_of(oc.gamma[1]), bits_of(oc.gamma[2]), bits_of(0.0f),
                                                     bits_of(oc.contrast), bits_of(oc.saturation)};
                    ++records;
                    const bool req = pc == ours;
                    records_equal += req;
                    if (!req)
                        std::printf("  record %+d %+d %+d %+d: program %08X %08X %08X %08X %08X %08X ours %08X %08X %08X %08X %08X %08X\n",
                                    b, k, s, t, pc[0], pc[1], pc[2], pc[3], pc[4], pc[5], ours[0], ours[1], ours[2], ours[3], ours[4], ours[5]);
                    if (s != 0) continue; // (the saturation is not in the levels)
                    for (float most : {255.0f, 235.0f, 220.0f}) {
                        if (most != 255.0f && (k != 0 || t != 0)) continue;
                        const auto pl = program_levels(ee, pc, most);
                        const float g[3] = {oc.gamma[0], oc.gamma[1], oc.gamma[2]};
                        const auto ol = race::correction_levels(g, 0, oc.contrast, most);
                        ++tables;
                        int off = 0;
                        for (size_t i = 0; i < 256; ++i)
                            for (size_t ch = 0; ch < 3; ++ch) off += pl[i][ch] != ol[i][ch];
                        levels_off += off;
                        tables_equal += off == 0;
                        if (off || (b == 3 && k == 0 && t == 0 && most == 255.0f && s == 0)) {
                            std::printf("  levels %+d %+d %+d %+d most %.0f: %d of 768 differ;", b, k, s, t, most, off);
                            int shown = 0;
                            for (size_t i = 0; i < 256 && shown < 8; ++i)
                                for (size_t ch = 0; ch < 3 && shown < 8; ++ch)
                                    if (pl[i][ch] != ol[i][ch]) std::printf(" [%zu.%zu] %d/%d", i, ch, pl[i][ch], ol[i][ch]), ++shown;
                            std::printf("\n    program 0..255 step 16 (green):");
                            for (size_t i = 0; i < 256; i += 16) std::printf(" %d", pl[i][1]);
                            std::printf(" %d\n", pl[255][1]);
                        }
                    }
                }
    // Made-up corrections as a course's and the Dive camera's records give them: any gammas, a black level, a white level.
    std::mt19937 rng(97328);
    std::uniform_real_distribution<float> gam(0.4f, 2.5f), blk(0.0f, 0.25f), wht(0.6f, 1.2f);
    for (int n = 0; n < 300; ++n) {
        const float g[3] = {gam(rng), gam(rng), gam(rng)};
        const float black = n % 3 == 0 ? 0.0f : blk(rng), white = n % 5 == 0 ? black : wht(rng);
        const float most = n % 3 == 0 ? 255.0f : n % 3 == 1 ? 235.0f : 220.0f;
        const std::array<u32, 6> c = {bits_of(g[0]), bits_of(g[1]), bits_of(g[2]), bits_of(black), bits_of(white), bits_of(1.0f)};
        const auto pl = program_levels(ee, c, most);
        const auto ol = race::correction_levels(g, black, white, most);
        ++tables;
        int off = 0;
        for (size_t i = 0; i < 256; ++i)
            for (size_t ch = 0; ch < 3; ++ch) off += pl[i][ch] != ol[i][ch];
        levels_off += off;
        tables_equal += off == 0;
        if (off) std::printf("  made-up %d (%.4f %.4f %.4f, %.4f .. %.4f, most %.0f): %d of 768 differ\n", n, g[0], g[1], g[2], black, white, most, off);
    }
    // What a table costs here (a new one each time: the Dive camera's colors move every frame).
    const auto t0 = std::chrono::steady_clock::now();
    int sum = 0;
    for (int n = 0; n < 200; ++n) {
        const float g[3] = {1.0f + float(n) * 0.001f, 1.1f, 0.9f};
        sum += race::correction_levels(g, 0, 1, 220)[100][0];
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 200;
    std::printf("ours: %.1f us a new table (%d)\n", us, sum);
    std::printf("records: %d of %d equal; tables: %d of %d equal (%d levels differ)\n", records_equal, records, tables_equal, tables,
                levels_off);
    return records_equal == records && tables_equal == tables;
}

} // namespace gt4r::diff
