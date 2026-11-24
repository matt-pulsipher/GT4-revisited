// The checks of gt4r_diff (diff.h): routines of the user's own CORE.GT4 run in the shared engine's
// R5900 interpreter (engine/gt3r/tools/diffharness) on the disc's own data, against ours. The
// routines are named by address (docs/specs/physics.md); nothing of the program is kept.

#include "diff.h"

#include "orig.h"

#include "disc/core_image.h"
#include "ee.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/car_parts.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/convert.h"
#include "gt4r/sim/exact_bridge.h"
#include "gt4r/sim/rules_build.h"
#include "taps.h"
#include "sim/exact/car_fields.h"
#include "sim/exact/start.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <type_traits>

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace gt4r::diff {

SpecResult run_spec(Disc& disc, bool print) {
    SpecResult result;
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) return result;
    result.ran = true;
    Ee ee;
    boot(ee, disc, sdb);
    const u32 eq_at = ee.alloc(0x200), spec_at = ee.alloc(0x600);
    size_t shown = 0, cars = 0, eq_same = 0, spec_same = 0, spec_same_given_eq = 0;
    std::map<u32, size_t> eq_bad, spec_bad;
    std::string first_eq, first_spec;
    for (const db::CarEntry& c : sdb.cars()) {
        sim::Equipment ours;
        if (!sim::stock_car(sdb, program, c.label, ours)) continue;
        ++cars;
        for (u32 o = 0; o < 0x200; o += 4) ee.w32(eq_at + o, 0);
        ee.call(kSetCar, {Arg::ptr(eq_at), Arg::integer(db::part_id(0, c.id))}, 50'000'000);
        std::array<u8, sim::Equipment::kSize> theirs{};
        ee.read_bytes(eq_at, theirs.data(), u32(theirs.size()));
        // (The gear words past the gearbox's count keep what the last car's generator left: not compared.)
        const u32 gears = db::part_row(sdb, ours.part(sim::Equipment::Gear))[0x24];
        for (u32 i = gears + 1; i < 12; ++i) theirs[0x110 + 2 * i] = ours.bytes[0x110 + 2 * i], theirs[0x111 + 2 * i] = ours.bytes[0x111 + 2 * i];
        bool same = true;
        for (u32 o = 0; o < theirs.size(); ++o)
            if (theirs[o] != ours.bytes[o]) {
                same = false, ++eq_bad[o];
                if (first_eq.empty()) first_eq = c.label;
            }
        eq_same += same;
        if (!same && shown++ < 6) {
            std::printf("  %s gears %d, slider %d; the program's:", c.label.c_str(), db::part_row(sdb, ours.part(sim::Equipment::Gear))[0x24], ours.b(sim::Equipment::GearSlider));
            for (u32 i = 0; i < 12; ++i) std::printf(" %d", gt3r::s16(theirs[0x110 + 2 * i] | (theirs[0x111 + 2 * i] << 8)));
            std::printf("; ours:");
            for (u32 i = 0; i < 12; ++i) std::printf(" %d", gt3r::s16(ours.h(0x110 + 2 * i)));
            std::printf("\n");
        }
        // The spec of the program's own equipment, by the program and by us.
        sim::Equipment given;
        given.bytes = theirs;
        for (int kind : {0, 2, 3}) {
            ee.call(kBuildSpec, {Arg::ptr(spec_at), Arg::ptr(eq_at), Arg::integer(0), Arg::integer(kind)}, 50'000'000);
            std::array<u8, sim::Spec::kSize> want{};
            ee.read_bytes(spec_at, want.data(), u32(want.size()));
            sim::Spec got;
            sim::build_spec(sdb, program, given, kind, got);
            bool equal = true;
            for (u32 o = 0; o < want.size(); ++o)
                if (want[o] != got.bytes[o]) {
                    equal = false, ++spec_bad[o];
                    if (first_spec.empty()) first_spec = c.label + (kind ? " (loose course)" : "");
                }
            if (kind == 0) spec_same_given_eq += equal;
            if (kind == 0 && same) spec_same += equal;
        }
    }
    result.cars = cars, result.equipment_equal = eq_same, result.spec_equal = spec_same_given_eq;
    if (print) {
        std::printf("spec: %zu cars; the equipment equal on %zu, the spec (of the program's equipment) equal on %zu\n", cars, eq_same, spec_same_given_eq);
        auto list = [](const char* what, const std::map<u32, size_t>& bad, const std::string& first) {
            if (bad.empty()) return;
            std::printf("  %s differs at (offset: cars):", what);
            size_t n = 0;
            for (const auto& [o, k] : bad)
                if (n++ < 60) std::printf(" %03X:%zu", o, k);
            std::printf("%s; first on %s\n", bad.size() > 60 ? " ..." : "", first.c_str());
        };
        list("the equipment", eq_bad, first_eq);
        list("the spec", spec_bad, first_spec);
    }
    (void)spec_same;
    return result;
}

// The parameter block: the program's converter on its spec against the engine's on ours translated, field by field.
ParamsResult run_params(Disc& disc, bool print) {
    ParamsResult result;
    namespace ex = gt3r::sim::exact;
    db::SpecDb sdb = open_db(disc);
    const sim::Program program = sim::Program::load(disc);
    if (!program.known()) return result;
    result.ran = true;
    Ee ee;
    boot(ee, disc, sdb);
    // The static tables as a race's configuration builds them (with a conductor: its per-surface tables too, which the
    // converter's top speed reads; a cleared conductor's course kind is a paved course's).
    {
        const u32 cond = ee.alloc(0x10200);
        for (u32 o = 0; o < 0x10200; o += 4) ee.w32(cond + o, 0);
        ee.call(kStatics, {Arg::ptr(cond)});
    }
    const ex::ProgramData core = sim::core_program(program);
    const u32 spec_at = ee.alloc(0x600), p_at = ee.alloc(0x1400), rules_at = ee.alloc(0x100), cond_at = ee.alloc(0x100);
    std::vector<Field>& fields = result.fields;
    auto field = [&](const char* name) -> Field& {
        for (Field& f : fields)
            if (f.name == name) return f;
        fields.push_back({name});
        return fields.back();
    };
    size_t cars = 0, trapped = 0;
    if (std::getenv("GT4R_LIST_UNPLACED")) { // the engine mirror's parameter fields without a place in Gran Turismo 4's block
        ex::Car probe{};
        ex::fields(probe, [&](const char* n, int i, u32 off, const auto&) {
            if (off < 0x10 || off >= 0x10 + ex::kParamsSize) return;
            if (sim::layout::place(off)) return;
            std::printf("  unplaced P+0x%03X %s[%d]\n", off - 0x10, n, i);
        });
    }
    for (const db::CarEntry& c : sdb.cars()) {
        sim::Equipment eq;
        if (!sim::stock_car(sdb, program, c.label, eq)) continue;
        sim::Spec spec;
        sim::build_spec(sdb, program, eq, 0, spec);
        // The program's: its converter on the same spec.
        for (u32 o = 0; o < 0x100; o += 4) ee.w32(rules_at + o, 0), ee.w32(cond_at + o, 0);
        ee.write_bytes(spec_at, spec.bytes);
        for (u32 o = 0; o < 0x1400; o += 4) ee.w32(p_at + o, 0);
        try {
            ee.call(kConvert, {Arg::ptr(p_at), Arg::ptr(spec_at), Arg::integer(0), Arg::integer(0), Arg::ptr(rules_at), Arg::ptr(cond_at)}, 200'000'000);
        } catch (const std::exception& e) {
            if (trapped++ < 3 && print) std::printf("  %s: %s\n", c.label.c_str(), e.what());
            continue;
        }
        // Gran Turismo 4's converter, ours, word by word against the program's block.
        {
            static const sim::rules::Tables race = sim::rules::tables(program, ex::Statics{});
            sim::ParamBlock ours;
            ours.base = p_at;
            sim::convert(spec, program, race, sim::ConvertIn{}, ours);
            std::vector<u8> theirs(sim::ParamBlock::kSize);
            ee.read_bytes(p_at, theirs.data(), u32(theirs.size()));
            bool same = true;
            for (u32 o = 0; o < sim::ParamBlock::kSize; o += 4) {
                if (std::memcmp(&theirs[o], &ours.bytes[o], 4) == 0) continue;
                same = false;
                if (result.block_off[o]++ == 0 && print && std::getenv("GT4R_BLOCK_EXAMPLES")) {
                    u32 x, y;
                    std::memcpy(&x, &theirs[o], 4), std::memcpy(&y, &ours.bytes[o], 4);
                    std::printf("  block +0x%04X %s: the program's %08X, ours %08X\n", o, c.label.c_str(), x, y);
                }
            }
            if (same) ++result.block_equal;
            // Again with what a race can give besides a car as sold: a computer's car (control source 2) with the rule
            // block's power cap and axle grips, a worn engine (its two condition words through every branch of the factor),
            // ballast and the entry's power and weight percentages.
            {
                sim::Spec v = spec;
                const u32 k = u32(cars % 5);
                v.set_b(0x05, 20 + 10 * k), v.set_b(0x06, u32(s32(-20) + 15 * s32(k))), v.set_b(0x07, 85 + 5 * k), v.set_b(0x08, 120 - 7 * k);
                sim::ConvertIn in;
                in.control = 2;
                in.rules[3] = u8(15 + 9 * k);
                const u8 grips[8] = {90, 110, 0, 120, 105, 95, 0, 80};
                for (u32 i = 0; i < 8; ++i) in.rules[4 + i] = grips[(i + k) % 8];
                std::array<u8, 0x20> cond{};
                const float w4[5] = {-400000.f, -100000.f, 500000.f, 2e6f, 2e7f}, w8[5] = {-200000.f, -50000.f, 1e6f, 5.5e6f, 9e6f};
                const gt3r::ee::F f4 = gt3r::ee::F(w4[k]), f8 = gt3r::ee::F(w8[(k + 2) % 5]);
                std::memcpy(&cond[4], &f4.bits, 4), std::memcpy(&cond[8], &f8.bits, 4);
                in.condition = cond.data();
                if (const char* only = std::getenv("GT4R_VARY")) { // (one kind of input varied alone)
                    const std::string o = only;
                    if (o != "cond") std::memset(&cond[4], 0, 8);
                    if (o != "cap") in.rules[3] = 0;
                    if (o != "spec") v = spec;
                    if (o != "grip") in.control = 0;
                }
                ee.write_bytes(spec_at, v.bytes);
                for (u32 i = 0; i < 0x80; ++i) ee.w8(rules_at + i, in.rules[i]);
                for (u32 i = 0; i < 0x20; ++i) ee.w8(cond_at + i, cond[i]);
                for (u32 o = 0; o < 0x1400; o += 4) ee.w32(p_at + o, 0);
                ee.call(kConvert, {Arg::ptr(p_at), Arg::ptr(spec_at), Arg::integer(in.control), Arg::integer(0), Arg::ptr(rules_at), Arg::ptr(cond_at)}, 200'000'000);
                sim::ParamBlock mine;
                mine.base = p_at;
                sim::convert(v, program, race, in, mine);
                std::vector<u8> prog(sim::ParamBlock::kSize);
                ee.read_bytes(p_at, prog.data(), u32(prog.size()));
                bool eq2 = true;
                for (u32 o = 0; o < sim::ParamBlock::kSize; o += 4) {
                    if (std::memcmp(&prog[o], &mine.bytes[o], 4) == 0) continue;
                    eq2 = false;
                    if (result.varied_off[o]++ == 0 && print && std::getenv("GT4R_BLOCK_EXAMPLES")) {
                        u32 x, y;
                        std::memcpy(&x, &prog[o], 4), std::memcpy(&y, &mine.bytes[o], 4);
                        std::printf("  varied +0x%04X %s: the program's %08X, ours %08X\n", o, c.label.c_str(), x, y);
                    }
                }
                if (eq2) ++result.varied_equal;
                // (the stock block back for the checks below)
                for (u32 o = 0; o < 0x100; o += 4) ee.w32(rules_at + o, 0), ee.w32(cond_at + o, 0);
                ee.write_bytes(p_at, theirs);
            }
            if (const char* dbg = std::getenv("GT4R_TOP_DEBUG"); dbg && c.label == dbg) {
                // The program's routines on the program's block: the engine's torque (0x34E9A0) and the rolling resistance's
                // curve (0x359830 on 0x620938) at a few speeds.
                for (float v : {20.0f, 50.0f, 80.0f, 89.0f, 90.0f, 94.0f}) {
                    const auto t = ee.call(0x34E9A0, {Arg::ptr(p_at + 0x134), Arg::ptr(cond_at), Arg::flt(v * 3)});
                    const auto r = ee.call(0x359830, {Arg::ptr(0x620938), Arg::flt(v)});
                    std::printf("  v %g: torque(%g) %08X, roll %08X, 0x620984 %08X\n", v, v * 3, t.f0, r.f0, ee.r32(0x620984));
                }
                std::printf("  FD8 program %08X ours %08X; D+0x12 %d gears %d up %d\n", ee.r32(p_at + 0xFD8), ours.w(0xFD8), ee.r8(p_at + 0x246), ee.r8(p_at + 0x236), ee.r16(p_at + 0x262));
                std::printf("  roll ours n %d", race.road.rolling_curve.n);
                for (int k = 0; k < 8; ++k) std::printf(" %08X/%08X", race.road.rolling_curve.x[k].bits, race.road.rolling_curve.y[k].bits);
                std::printf("\n  roll prog n %d", ee.r16(0x620938));
                for (u32 k = 0; k < 8; ++k) std::printf(" %08X/%08X", ee.r32(ee.r32(0x62093C) + 4 * k), ee.r32(ee.r32(0x620940) + 4 * k));
                std::printf("\n");
            }
        }
        // Ours: the engine's converter on the translated spec, then Gran Turismo 4's own rules.
        ex::Spec cs;
        sim::core_spec(spec, cs);
        ex::ConvertInputs in;
        in.data = &core, in.base = 0x01000010;
        in.physics_globals();
        ex::ParamImage image{};
        ex::convert_params(cs, in, image);
        ex::Record record{};
        std::copy(image.begin(), image.end(), record.begin() + ex::kParams);
        ex::Car car;
        ex::load_image(car, record.data());
        sim::gt4_params(spec, program, car.p);
        const ex::Params& p = car.p;
        ++cars;
        // The record the session built before (the engine's converter, gt4_params, gt4_extras) against the record from
        // Gran Turismo 4's block (sim::build_record), field by field: what changes for the step.
        if (std::getenv("GT4R_RECORD_CHECK")) {
            static const sim::rules::Tables race = sim::rules::tables(program, ex::Statics{});
            sim::Record before{car, {}, {}};
            sim::gt4_extras(spec, program, before);
            sim::Record after{};
            sim::build_record(spec, program, race, sim::ConvertIn{}, after);
            auto cmp = [&](const char* n, const void* a, const void* b, size_t size) {
                Field& f = field((std::string("record ") + n).c_str());
                ++f.values;
                if (std::memcmp(a, b, size) == 0) return;
                ++f.off;
                if (f.where.empty()) {
                    u32 x = 0, y = 0;
                    std::memcpy(&x, a, std::min<size_t>(size, 4)), std::memcpy(&y, b, std::min<size_t>(size, 4));
                    char t[200];
                    std::snprintf(t, sizeof t, "%s: before %08X, now %08X", c.label.c_str(), x, y);
                    f.where = t;
                }
            };
            const u8* bb = reinterpret_cast<const u8*>(&before);
            const u8* ab = reinterpret_cast<const u8*>(&after);
            ex::fields(before.car, [&](const char* n, int, u32 off, const auto& m) {
                if (off < 0x10 || off >= 0x10 + ex::kParamsSize) return;
                using T = std::remove_cvref_t<decltype(m)>;
                const size_t at = size_t(reinterpret_cast<const u8*>(&m) - bb);
                cmp((std::string("mirror ") + n).c_str(), bb + at, ab + at, std::is_same_v<T, gt3r::ee::F> ? 4 : sizeof(T));
            });
            sim::extra_fields(before.x, [&](const char* n, int, sim::layout::Block, u32, const auto& m) {
                using T = std::remove_cvref_t<decltype(m)>;
                const size_t at = size_t(reinterpret_cast<const u8*>(&m) - bb);
                cmp((std::string("extra ") + n).c_str(), bb + at, ab + at, std::is_same_v<T, gt3r::ee::F> ? 4 : sizeof(T));
            });
            cmp("layout", &before.layout, &after.layout, sizeof before.layout);
            cmp("chassis.mass", &before.x.chassis.mass, &after.x.chassis.mass, 4);
            cmp("body.sprung_mass", &before.x.body.sprung_mass, &after.x.body.sprung_mass, 4);
        }
        // Gran Turismo 4's own part of the record (rules_build.h), bit for bit, every field the parameter block holds.
        {
            // (the record as the session builds it: Gran Turismo 4's converter, build_record)
            static const sim::rules::Tables race_tables = sim::rules::tables(program, ex::Statics{});
            sim::Record ours{car, {}, {}};
            sim::build_record(spec, program, race_tables, sim::ConvertIn{}, ours);
            Image im;
            im.car.assign(sim::layout::kCarSize, 0), im.params.resize(sim::layout::kParamsSize);
            ee.read_bytes(p_at, im.params.data(), u32(im.params.size()));
            sim::Record theirs{};
            load(theirs, im);
            const size_t x0 = size_t(reinterpret_cast<const u8*>(&ours.x) - reinterpret_cast<const u8*>(&ours));
            for (const Slot& sl : slots()) {
                if (sl.place.block != sim::layout::Block::Params) continue;
                // (the engine mirror's fields with a place: listed apart, "mirror", and only with GT4R_PARAMS_MIRROR)
                const bool mirror = sl.member < x0;
                if (mirror && !std::getenv("GT4R_PARAMS_MIRROR")) continue;
                const std::string name = sl.name.substr(0, sl.name.find('['));
                Field& f = field(((mirror ? "mirror " : "extra ") + name).c_str());
                ++f.values;
                const u8* a = reinterpret_cast<const u8*>(&theirs) + sl.member;
                const u8* b = reinterpret_cast<const u8*>(&ours) + sl.member;
                // The driveline block's values also by size (the engine's converter's arithmetic, a unit or two in the
                // last place on many): "near d.<name>".
                if (name.rfind("d.", 0) == 0 && (sl.real || sl.size == 2)) {
                    double x = 0, y = 0;
                    if (sl.real) {
                        gt3r::ee::F fa, fb;
                        std::memcpy(&fa, a, 4), std::memcpy(&fb, b, 4);
                        x = double(fa.host()), y = double(fb.host());
                    } else {
                        u16 ha = 0, hb = 0;
                        std::memcpy(&ha, a, 2), std::memcpy(&hb, b, 2);
                        x = ha, y = hb;
                    }
                    Field& nf = field(("near " + name).c_str());
                    ++nf.values;
                    const double dd = std::fabs(x - y), rel = dd / std::max(std::fabs(x), 1e-9);
                    if (dd > 1e-6 && rel > (sl.real ? 1e-5 : 1e-3)) {
                        ++nf.off;
                        if (std::getenv("GT4R_LIST_NEAR")) std::printf("  near %s %s: %g / %g\n", c.label.c_str(), sl.name.c_str(), x, y);
                        if (rel > nf.worst) nf.worst = rel, nf.where = c.label + " " + sl.name + ": the program's " + std::to_string(x) + ", ours " + std::to_string(y);
                    }
                }
                if (std::memcmp(a, b, sl.size) != 0) {
                    ++f.off;
                    if (f.where.empty()) {
                        u32 x = 0, y = 0;
                        std::memcpy(&x, a, sl.size), std::memcpy(&y, b, sl.size);
                        char t[200];
                        std::snprintf(t, sizeof t, "%s %s: the program's %08X, ours %08X", c.label.c_str(), sl.name.c_str(), x, y);
                        f.where = t;
                    }
                }
            }
            auto bits = [&](const char* name, u32 at, gt3r::ee::F v) {
                Field& f = field(name);
                ++f.values;
                if (ee.r32(p_at + at) != v.bits) {
                    ++f.off;
                    if (f.where.empty()) f.where = c.label;
                }
            };
            bits("bits static load front", 0x1040, p.xe54), bits("bits static load rear", 0x1044, p.xe58);
            bits("bits unsprung front", 0x344, p.susp[0].preload), bits("bits unsprung rear", 0x344 + 0x58, p.susp[1].preload);
            Field& lf = field("extra wheel layout row");
            ++lf.values;
            if (ee.r8(p_at + 0x30) != sim::wheel_layout_index(spec)) ++lf.off, lf.where = c.label;
        }
        // Equal: within a part in 100,000 (ours is in the same arithmetic but not everywhere in the same order).
        auto check = [&](const char* name, double theirs, double ours, double within = 1e-5) {
            Field& f = field(name);
            ++f.values;
            const double d = std::fabs(theirs - ours), rel = d / std::max(std::fabs(theirs), 1e-9);
            if (d > 1e-6 && rel > within) {
                ++f.off;
                if (rel > f.worst) f.worst = rel, f.where = c.label + ": the program's " + std::to_string(theirs) + ", ours " + std::to_string(ours);
            }
        };
        auto pf = [&](u32 o) { return double(ee.rf(p_at + o)); };
        check("body front", pf(0x00), p.body_front.host()), check("body rear", pf(0x04), p.body_rear.host());
        check("body width", pf(0x08), p.body_width.host()), check("body height", pf(0x0C), p.body_height.host());
        check("CoG to front axle", pf(0x10), p.dist_front.host()), check("CoG to rear axle", pf(0x14), p.dist_rear.host());
        check("wheelbase", pf(0x18), p.wheelbase.host()), check("track", pf(0x1C), p.track.host());
        check("half-track front", pf(0x28), p.half_track[0].host()), check("half-track rear", pf(0x2C), p.half_track[1].host());
        check("lateral transfer", pf(0x34), p.lat_transfer.host()), check("longitudinal transfer", pf(0x38), p.long_transfer.host());
        check("CoG height", pf(0x3C), p.cog_height.host());
        check("ride height front", pf(0x40), p.ride_height[0].host()), check("ride height rear", pf(0x44), p.ride_height[1].host());
        check("mass", pf(0x48), p.mass.host()), check("origin offset", pf(0x50), p.x54.host()), check("steering lock", pf(0x54), p.max_lock.host());
        check("yaw inertia", pf(0x114), p.yaw_inertia.host());
        check("pitch lever front", pf(0x11C), p.pitch_lever[0].host()), check("pitch lever rear", pf(0x120), p.pitch_lever[1].host());
        check("roll lever front", pf(0x124), p.roll_lever[0].host()), check("roll lever rear", pf(0x128), p.roll_lever[1].host());
        check("pitch tire term front", pf(0x12C), p.pitch_tire[0].host()), check("pitch tire term rear", pf(0x130), p.pitch_tire[1].host());
        check("weight", pf(0x103C), p.weight.host());
        check("static load front", pf(0x1040), p.xe54.host()), check("static load rear", pf(0x1044), p.xe58.host());
        check("drag", pf(0x1068), p.drag_coef.host());
        check("downforce front", pf(0x106C), p.downforce_coef[0].host()), check("downforce rear", pf(0x1070), p.downforce_coef[1].host());
        check("brake torque front", pf(0x1010), p.brake_torque[0].host()), check("brake torque rear", pf(0x1014), p.brake_torque[1].host());
        check("handbrake torque", pf(0x1018), p.handbrake_torque.host());
        check("axle grip front", pf(0xFD0), p.axle_grip[0].host()), check("axle grip rear", pf(0xFD4), p.axle_grip[1].host());
        check("top speed", pf(0xFD8), p.top_speed.host());
        // The engine (the program's block at +0x134: the curve's points, speeds from +0x0C, torques from +0x6C).
        const u32 E = p_at + 0x134;
        const int n = gt3r::s16(ee.r16(E));
        check("engine curve points", n, p.e.torque.n);
        if (n == p.e.torque.n)
            for (int i = 0; i < n && i < 16; ++i) {
                check("engine curve speeds", double(ee.rf(E + 0x0C + 4 * u32(i))), p.e.torque.x[size_t(i)].host());
                check("engine curve torques", double(ee.rf(E + 0x6C + 4 * u32(i))), p.e.torque.y[size_t(i)].host());
            }
        check("rev limit", ee.r16(E + 0xCC), p.e.rev_limit), check("idle", ee.r16(E + 0xCE), p.e.idle);
        check("engine friction", double(ee.rf(E + 0xD0)), p.e.friction.host());
        // The driveline (+0x234): gears, each gear's speed factor (+0x68), tire radius (+0x90) and width (+0xA0).
        const u32 D = p_at + 0x234;
        if (std::getenv("GT4R_LIST_DRIVES") && (ee.r8(D + 0x12) != 0 || ee.r8(D) > 6 || ee.r8(D + 2) > 7 || gt3r::s16(ee.r16(p_at + 0x134)) > 16))
            std::printf("  drive %s: layout %d, kind %d, gears %d, curve %d, special %d\n", c.label.c_str(), ee.r8(D), ee.r8(D + 0x12), ee.r8(D + 2),
                        gt3r::s16(ee.r16(p_at + 0x134)), ee.r8(p_at + 0x32));
        check("forward gears", ee.r8(D + 2), p.d.gears);
        check("driveline layout", ee.r8(D), p.d.layout);
        for (u32 g = 0; g <= ee.r8(D + 2) && g <= p.d.gears && g < 8; ++g) check("gear speed factors", double(ee.rf(D + 0x68 + 4 * g)), p.d.gear_speed[g].host(), 1e-3); // (a second reduction: to a ratio's last digit)
        check("tire radius front", double(ee.rf(D + 0x90)), p.tire_radius[0].host()), check("tire radius rear", double(ee.rf(D + 0x94)), p.tire_radius[1].host());
        check("tire width front", double(ee.rf(D + 0xA0)), p.width[0].host()), check("tire width rear", double(ee.rf(D + 0xA4)), p.width[1].host());
        check("engine inertia", double(ee.rf(D + 0x0C)), p.d.engine_inertia.host());
        // The rest of the driveline: the engine's block (Gran Turismo 3's, +0x1F8) up to the down-shift speeds, then eight
        // of them (the gears 2 .. 9), the ratios and speed factors of reverse and nine gears, and the rest 0x14 further on.
        check("driveline second axle byte", ee.r8(D + 1), p.d.unequal), check("driven axle", ee.r8(D + 3), p.d.driven_axle);
        check("gearbox kind", ee.r8(D + 0x12), p.d.kind);
        check("awd split", double(ee.rf(D + 0x04)), p.d.awd_k.host()), check("awd capacity", double(ee.rf(D + 0x08)), p.d.awd_capacity.host());
        for (u32 a = 0; a < 2; ++a) {
            check("diff type", ee.r8(D + 0x10 + a), p.d.diff_type[a]);
            check("diff initial", double(ee.rf(D + 0x14 + 4 * a)), p.d.diff_initial[a].host());
            check("diff accel", double(ee.rf(D + 0x1C + 4 * a)), p.d.diff_accel[a].host());
            check("diff decel", double(ee.rf(D + 0x24 + 4 * a)), p.d.diff_decel[a].host());
            check("inverse tire radius", double(ee.rf(D + 0x98 + 4 * a)), p.inv_tire_radius[a].host());
            check("wheel gain", double(ee.rf(D + 0xB4 + 4 * a)), p.d.wheel_gain[a].host());
            check("axle inertia", double(ee.rf(D + 0xBC + 4 * a)), p.d.axle_inertia[a].host());
        }
        check("launch rpm", ee.r16(D + 0x2C), p.d.launch_rpm), check("up-shift rpm", ee.r16(D + 0x2E), p.d.up_rpm);
        for (u32 g = 0; g < 6 && g + 2 <= p.d.gears; ++g) check("down-shift rpm", ee.r16(D + 0x30 + 2 * g), p.d.down_rpm[g]);
        for (u32 g = 0; g <= ee.r8(D + 2) && g <= p.d.gears && g < 8; ++g) check("gear ratios", double(ee.rf(D + 0x40 + 4 * g)), p.d.ratio[g].host(), 1e-3);
        check("shift clutch torque", double(ee.rf(D + 0xB0)), p.d.shift_clutch_torque.host());
        check("inverse axle inertia", double(ee.rf(D + 0xC4)), p.d.inv_axle_inertia.host());
        check("inverse engine inertia", double(ee.rf(D + 0xC8)), p.d.inv_engine_inertia.host());
        // The engine's block past its curve: the engine's (+0x138 + 0x94 ..) 0x40 further on (24 points for 16).
        for (u32 s = 0; s < 2; ++s) {
            check("turbo knee", gt3r::s16(ee.r16(E + 0xD4 + 2 * s)), p.e.turbo_knee[s]), check("turbo shape", ee.r8(E + 0xD8 + s), p.e.turbo_shape[s]);
            check("turbo peak", double(ee.rf(E + 0xE4 + 4 * s)), p.e.turbo_peak[s].host()), check("turbo rate", double(ee.rf(E + 0xEC + 4 * s)), p.e.turbo_rate[s].host());
            check("turbo coefficient", double(ee.rf(E + 0xF4 + 4 * s)), p.e.turbo_coef[s].host());
        }
        check("power cap switch", ee.r8(E + 0xDB), p.e.power_cap_on), check("power cap", double(ee.rf(E + 0xDC)), p.e.power_cap.host());
        check("power cap soft", double(ee.rf(E + 0xE0)), p.e.power_cap_soft.host()), check("boost max", double(ee.rf(E + 0xFC)), p.e.boost_max.host());
        // The tire records (+0x410 + 0x1EC x record; the core's record with 24 surfaces, what follows them 0x24 further on):
        // those the core built (a surface's class names them), surface by surface as the core's rows stand for them.
        for (u32 r = 0; r < 6; ++r) {
            const ex::TireRecord& t = p.tire[r];
            if (t.mu[0] == gt3r::ee::F()) continue;
            const u32 T = p_at + 0x410 + 0x1EC * r;
            auto tf = [&](u32 o) { return double(ee.rf(T + o)); };
            check("tire base grip", tf(0x00), t.grip.host()), check("tire wear rate", tf(0x04), t.wear_rate.host());
            for (int row = 0; row < 16; ++row) {
                if (row == 11) continue; // (the wear grip's floor in the core's record)
                check("tire grip by surface", tf(0x08 + 4 * u32(sim::surface_of_core_row(row))), t.mu[size_t(row)].host());
            }
            for (u32 i = 0; i < 8; ++i) check("tire curve A", tf(0x78 + 4 * i) + tf(0x98 + 4 * i), double(t.curve_a.x[i].host()) + double(t.curve_a.y[i].host()));
            check("tire peak slip angle", tf(0xB8), t.peak_slip.host());
            for (u32 i = 0; i < 6; ++i) check("tire curve B", tf(0xC8 + 4 * i) + tf(0xE0 + 4 * i), double(t.curve_b.x[i].host()) + double(t.curve_b.y[i].host()));
            for (u32 i = 0; i < 11; ++i) {
                check("tire curve C", tf(0x104 + 4 * i) + tf(0x134 + 4 * i), double(t.curve_c.x[i].host()) + double(t.curve_c.y[i].host()));
                check("tire curve D", tf(0x17C + 4 * i), t.curve_d[i].host());
            }
            check("tire optimum slips", tf(0x164) + tf(0x168), double(t.slip_drive.host()) + double(t.slip_brake.host()));
            check("tire braking peak", tf(0x16C), t.brake_peak.host());
            for (u32 i = 0; i < 4; ++i) check("tire load curve", tf(0x1B8 + 4 * i) * 1e-4 + tf(0x1C8 + 4 * i), double(t.load_curve.x[i].host()) * 1e-4 + double(t.load_curve.y[i].host()));
        }
        // The suspension blocks (+0x308 + 0x58 x axle), the fields the core's blocks have at the same places.
        for (u32 a = 0; a < 2; ++a) {
            const u32 B = p_at + 0x308 + 0x58 * a;
            const ex::Suspension& u = p.susp[a];
            auto bf = [&](u32 o) { return double(ee.rf(B + o)); };
            check(a ? "rear knee" : "front knee", bf(0x00), u.knee.host());
            check(a ? "rear bump limit" : "front bump limit", bf(0x04), u.bump_limit.host());
            check(a ? "rear droop" : "front droop", bf(0x08), u.droop.host());
            check(a ? "rear offset" : "front offset", bf(0x0C), u.offset.host());
            check(a ? "rear static travel" : "front static travel", bf(0x10), u.x_static.host());
            check(a ? "rear spring rate" : "front spring rate", bf(0x14), u.k.host());
            check(a ? "rear damper bound" : "front damper bound", bf(0x18) + bf(0x1C) * 1e-3 + bf(0x20) * 1e-6, u.bump_t.host() + u.bump_o.host() * 1e-3 + u.bump_g.host() * 1e-6);
            check(a ? "rear damper rebound" : "front damper rebound", bf(0x24) + bf(0x28) * 1e-3 + bf(0x2C) * 1e-6, u.reb_t.host() + u.reb_o.host() * 1e-3 + u.reb_g.host() * 1e-6);
            check(a ? "rear anti-roll" : "front anti-roll", bf(0x30), u.anti_roll.host());
            check(a ? "rear bump-stop rate" : "front bump-stop rate", bf(0x34), u.k2.host());
            check(a ? "rear unsprung weight" : "front unsprung weight", bf(0x3C), u.preload.host());
            check(a ? "rear bump damping" : "front bump damping", bf(0x40), u.bump_damping.host());
        }
    }
    result.cars = cars, result.unfinished = trapped;
    if (print) {
        std::printf("params: %zu cars (%zu the program's converter did not finish)\n", cars, trapped);
        std::printf("  block: %zu of %zu cars equal in every word\n", result.block_equal, cars);
        for (const auto& [o, n] : result.block_off) std::printf("  block +0x%04X differs on %zu cars\n", o, n);
        std::printf("  block, varied inputs: %zu of %zu cars equal in every word\n", result.varied_equal, cars);
        for (const auto& [o, n] : result.varied_off) std::printf("  varied +0x%04X differs on %zu cars\n", o, n);
        for (const Field& f : fields) {
            if (f.off == 0) std::printf("  %-24s equal (%zu values)\n", f.name.c_str(), f.values);
            else std::printf("  %-24s differs on %zu of %zu, most by %.3g%% (%s)\n", f.name.c_str(), f.off, f.values, f.worst * 100, f.where.c_str());
        }
    }
    return result;
}

} // namespace gt4r::diff
