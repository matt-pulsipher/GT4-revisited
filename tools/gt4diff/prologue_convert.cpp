// GT4R_CONVERT: Prologue's converter (0x3C74B8) against ours (sim/convert_prologue.h) on every car of its database: the
// program's spec builder (0x3B3998) makes each car's spec in the interpreter, the program's converter and ours each make the
// parameter block from it, and every word is compared. Also the race's start's values the converter reads.

#include "orig.h"
#include "prologue_race.h"

#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/convert_prologue.h"
#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_prologue.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

namespace gt4r::diff::prologue {

namespace {
constexpr u32 kSpecBuilder = 0x3B3998, kConverter = 0x3C74B8, kSetCar = 0x407A08;
}

int convert_check(Disc& disc, const std::string& course, const std::string& car) {
    db::SpecDb sdb = open_db(disc);
    Ee ee;
    boot(ee, disc);
    struct Args {
        bool seen = false;
        u32 a2 = 0, a3 = 0, t0 = 0, t1 = 0;
    } conv, spec_args;
    ee.probe(kConverter, [&](Ee& e) {
        if (!conv.seen) conv = {true, u32(e.a(2)), u32(e.a(3)), u32(e.g32(8)), u32(e.g32(9))};
    });
    ee.probe(kSpecBuilder, [&](Ee& e) {
        if (!spec_args.seen) spec_args = {true, u32(e.a(2)), u32(e.a(3)), 0, 0};
    });
    Race r;
    std::string err;
    if (!setup_race(ee, disc, sdb, course, {car}, 1, r, err)) {
        std::printf("prologue convert: %s\n", err.c_str());
        return 1;
    }
    ee.clear_probes();
    std::printf("prologue convert: the converter's a2 %08X a3 %08X t0 %08X t1 %08X; the spec builder's a2 %08X a3 %08X\n", conv.a2, conv.a3, conv.t0,
                conv.t1, spec_args.a2, spec_args.a3);
    const sim::Program program = sim::Program::load(disc);
    const sim::prologue::ConvertData data = sim::prologue::convert_data(program);
    // The race's start's values against ours.
    {
        int off = 0;
        auto check = [&](const char* what, u32 theirs, u32 ours) {
            if (theirs != ours) ++off, std::printf("  %s: the program's %08X, ours %08X\n", what, theirs, ours);
        };
        check("air density 0x504A74", ee.r32(0x504A74), data.air_density.bits);
        check("rolling scale 0x504438", ee.r32(0x504438), data.roll_scale.bits);
        const u32 n = ee.r16(0x504428), xs = ee.r32(0x50442C), ys = ee.r32(0x504430);
        check("rolling points", n, data.roll_n);
        for (u32 k = 0; k < n && k < 8; ++k) {
            check("rolling x", ee.r32(xs + 4 * k), data.roll_x[k].bits);
            check("rolling y", ee.r32(ys + 4 * k), data.roll_y[k].bits);
        }
        std::printf("prologue convert: the race's start's values: %s\n", off ? "differ" : "equal");
    }
    sim::prologue::ConvertIn in;
    in.control = s32(conv.a2);
    if (conv.t0)
        for (u32 k = 0; k < in.rules.size(); ++k) in.rules[k] = ee.r8(conv.t0 + k);
    if (conv.t1)
        for (u32 k = 0; k < in.condition.size(); ++k) in.condition[k] = ee.r32(conv.t1 + 4 * k);
    const bool verbose = std::getenv("GT4R_CONVERT_WORDS") != nullptr;
    const char* only = std::getenv("GT4R_CONVERT_CAR");
    int cars = 0, equal_cars = 0;
    std::map<u32, int> off_words; // block offset -> cars where it differs
    std::map<std::string, std::map<u32, int>> spec_bytes;
    std::map<u32, int> spec_off_bytes; // our spec's byte offset -> cars where it differs from the program's
    int spec_cars = 0;
    for (const db::CarEntry& c : sdb.cars()) {
        if (only && c.label != only) continue;
        const u32 eq = ee.alloc(0x400, 64), spec = ee.alloc(0x400, 64), block = ee.alloc(sim::prologue::kBlockSize + 0x40, 64);
        std::vector<u8> zero(0x400, 0);
        ee.write_bytes(eq, ByteSpan(zero.data(), zero.size()));
        ee.write_bytes(spec, ByteSpan(zero.data(), zero.size()));
        std::vector<u8> zb(sim::prologue::kBlockSize, 0);
        ee.write_bytes(block, ByteSpan(zb.data(), zb.size()));
        try {
            ee.call(kSetCar, {Arg::ptr(eq), Arg::integer(s64(db::part_id(0, c.id)))}, 50'000'000);
            ee.call(kSpecBuilder, {Arg::ptr(spec), Arg::ptr(eq), Arg::integer(spec_args.a2), Arg::integer(spec_args.a3)}, 50'000'000);
        } catch (const std::exception& e) {
            std::printf("  %s: the spec builder stopped: %s\n", c.label.c_str(), e.what());
            continue;
        }
        if (std::getenv("GT4R_CONVERT_LABEL") && c.label == std::getenv("GT4R_CONVERT_LABEL")) {
            const u32 str = ee.alloc_cstr(c.label);
            const auto res = ee.call(0x409F10, {Arg::ptr(str)});
            std::printf("  %s: the equipment's car %016llX, the label's id %016llX\n", c.label.c_str(), (unsigned long long)ee.r64(eq),
                        (unsigned long long)res.v0.lo);
        }
        gt3r::sim::exact::Spec s{};
        ee.read_bytes(spec, s.data(), u32(s.size()));
        for (u32 k : {0x0Au, 0x11u, 0x12u, 0xB2u, 0xB3u, 0xCAu, 0xE1u, 0x2Du})
            ++spec_bytes[c.label.substr(0, 0)][(k << 8) | s[k]];
        try {
            ee.call(kConverter, {Arg::ptr(block), Arg::ptr(spec), Arg::integer(conv.a2), Arg::integer(conv.a3), Arg::ptr(conv.t0), Arg::ptr(conv.t1)},
                    200'000'000);
        } catch (const std::exception& e) {
            std::printf("  %s: the converter stopped: %s\n", c.label.c_str(), e.what());
            continue;
        }
        // Our spec (Gran Turismo 4's builder on the database, in the core's layout) against the program's.
        {
            sim::Equipment eq_ours;
            if (sim::stock_car(sdb, program, c.label, eq_ours)) {
                sim::Spec ours_spec;
                sim::build_spec(sdb, program, eq_ours, 0, ours_spec);
                gt3r::sim::exact::Spec core{};
                sim::prologue::core_spec(ours_spec, c.label, 0, data, core);
                int n = 0;
                for (u32 k = 0; k < core.size(); ++k)
                    if (core[k] != s[k]) {
                        ++n, ++spec_off_bytes[k];
                        if (std::getenv("GT4R_CONVERT_SPEC")) std::printf("    %s spec +%03X: the program's %3u, ours %3u\n", c.label.c_str(), k, s[k], core[k]);
                    }
                if (n) ++spec_cars;
                if (std::getenv("GT4R_CONVERT_GEARS") && s[0xE1] != 0) {
                    std::printf("    %s E1 %u; ours +0x150..: ", c.label.c_str(), s[0xE1]);
                    for (u32 k = 0x150; k < 0x162; ++k) std::printf("%02X ", ours_spec.b(k));
                    std::printf("\n");
                }
            }
        }
        sim::prologue::Block theirs{}, ours{};
        ee.read_bytes(block, theirs.data(), u32(theirs.size()));
        gt3r::sim::exact::Spec theirs_spec{};
        ee.read_bytes(spec, theirs_spec.data(), u32(theirs_spec.size()));
        sim::prologue::convert(s, data, in, block, ours);
        ++cars;
        int off = 0;
        for (u32 o = 0; o + 4 <= ours.size(); o += 4) {
            u32 a, b;
            std::memcpy(&a, &ours[o], 4), std::memcpy(&b, &theirs[o], 4);
            if (a == b) continue;
            ++off, ++off_words[o];
            if (verbose && off <= 40) {
                float fa, fb;
                std::memcpy(&fa, &a, 4), std::memcpy(&fb, &b, 4);
                std::printf("    %s P+%03X  ours %08X (%g)  the program's %08X (%g)\n", c.label.c_str(), o, a, double(fa), b, double(fb));
            }
        }
        int spec_off = 0;
        for (size_t k = 0; k < s.size(); ++k) spec_off += s[k] != theirs_spec[k];
        if (off == 0 && spec_off == 0) ++equal_cars;
        else std::printf("  %-20s %d words differ, the spec after: %d bytes\n", c.label.c_str(), off, spec_off);
    }
    std::printf("prologue convert: %d of %d cars' blocks equal word for word\n", equal_cars, cars);
    for (const auto& [o, n] : off_words) std::printf("    P+%03X differs on %d cars\n", o, n);
    std::printf("prologue convert: our spec differs from the program's on %d cars\n", spec_cars);
    for (const auto& [o, n] : spec_off_bytes) std::printf("    spec +%03X differs on %d cars\n", o, n);
    std::printf("prologue convert: the spec's bytes (offset:value cars):");
    for (const auto& [k, n] : spec_bytes[""]) std::printf(" %02X:%02X %d", k >> 8, k & 0xFF, n);
    std::printf("\n");
    return equal_cars == cars ? 0 : 1;
}

} // namespace gt4r::diff::prologue
