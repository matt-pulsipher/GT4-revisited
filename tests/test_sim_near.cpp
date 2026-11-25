// Phase 14 (which cars are near which, the slipstream) and phase 15 (the part that changes with speed) against the user's
// own program (rules_near.h): the converter's values of the part, the damping rows, and the routines at every call of the
// program's own race step (tools/gt4diff taps_near.cpp). Reads the Gran Turismo 4 disc of GT4R_DISC; skips without it.

#include "test_framework.h"

#include "diff.h"
#include "orig.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_near.h"

#include <cstdlib>

using namespace gt4r;

namespace {

Disc* full_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

} // namespace

TEST(sim_near_speed_part_params_and_damping_rows) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb sdb = diff::open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    diff::Ee ee;
    diff::boot(ee, *disc, sdb);
    ee.call(diff::kStatics, {diff::Arg::integer(0)});
    // The damping rows as the race's start builds them (0x3F2390), against ours from the program's bytes.
    ee.call(0x3F2390, {});
    const sim::rules::SpeedDampTables theirs =
        sim::rules::speed_damp_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
    CHECK(sim::rules::same(sim::rules::speed_damp_tables(program), theirs));
    // Every car's part as the program's converter makes it (P + 0x1077 .. + 0x1080) against ours from its spec.
    const u32 spec_at = ee.alloc(0x600), p_at = ee.alloc(0x1400), rules_at = ee.alloc(0x100), cond_at = ee.alloc(0x100);
    size_t cars = 0, equal = 0, with_part = 0, with_damping = 0;
    const bool list = std::getenv("GT4R_NEAR_LIST") != nullptr;
    for (const db::CarEntry& c : sdb.cars()) {
        sim::Equipment eq;
        if (!sim::stock_car(sdb, program, c.label, eq)) continue;
        sim::Spec spec;
        sim::build_spec(sdb, program, eq, 0, spec);
        ee.write_bytes(spec_at, spec.bytes);
        for (u32 o = 0; o < 0x1400; o += 4) ee.w32(p_at + o, 0);
        ee.call(diff::kConvert, {diff::Arg::ptr(p_at), diff::Arg::ptr(spec_at), diff::Arg::integer(0), diff::Arg::integer(0), diff::Arg::ptr(rules_at),
                                 diff::Arg::ptr(cond_at)},
                200'000'000);
        sim::rules::NearExtra x;
        sim::rules::speed_part_params(x, spec);
        ++cars;
        const bool same = ee.r8(p_at + 0x1077) == spec.b(0x4C) && ee.r32(p_at + 0x1078) == x.speed_lo.bits && ee.r32(p_at + 0x107C) == x.speed_hi.bits &&
                          ee.r32(p_at + 0x1080) == x.speed_rate.bits;
        equal += same;
        if (!same) std::printf("  %s: the part differs\n", c.label.c_str());
        with_part += spec.b(0x4C) != 0, with_damping += ee.r8(p_at + 0x1076) != 0;
        if (list) { // (the rally cars, whose damping row on a loose course is 3)
            sim::Spec loose;
            sim::build_spec(sdb, program, eq, 2, loose);
            if (loose.b(0xA8) == 3) std::printf("  %s: damping row 3 on a loose course\n", c.label.c_str());
        }
        if (list && (spec.b(0x4C) != 0 || ee.r8(p_at + 0x1076) != 0))
            std::printf("  %s: kind %u (%u .. %u km/h, rate byte %u), damping row %u\n", c.label.c_str(), spec.b(0x4C), spec.b(0x4D), spec.b(0x4E), spec.b(0x4F),
                        ee.r8(p_at + 0x1076));
    }
    std::printf("  %zu cars, %zu with the part, %zu with its damping\n", cars, with_part, with_damping);
    CHECK(cars > 700 && equal == cars);
}

TEST(sim_near_speed_part_equal_in_the_step) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The program's own race step with the cars whose part changes with speed (kinds 1, 2, 4, 5, 6) and those with its damping
    // (rows 1, 2, 4; row 3: the MTR GMV on a loose course): phase 15 (0x3F28B8, 0x3F2798), the damping (0x3F2650, 0x3F24A0) and
    // phase 14 on the one car, equal at every call.
    struct Run {
        const char* course;
        const char* car;
        bool damping;
    };
    for (const Run& run : {Run{"tsukuba", "lemans_quattro_03", false}, Run{"tsukuba", "cien_dmv_02", false}, Run{"tsukuba", "slr_mclaren_03", false},
                           Run{"tsukuba", "hsr2_concept_89", false}, Run{"tsukuba", "nike_one_04", true}, Run{"tsukuba", "sl55_amg_02", true},
                           Run{"grandcanyon", "mtr_gmv_04", true}}) {
        const diff::StepResult r = diff::run_step(*disc, false, run.course, run.car, 900);
        if (!r.ran) std::printf("  %s: %s\n", run.car, r.error.c_str());
        CHECK(r.ran && !r.taps.empty());
        size_t part = 0, damping = 0;
        for (const diff::StepTap& t : r.taps) {
            if (t.name.rfind("near", 0) != 0 && t.name.rfind("speed part", 0) != 0) continue;
            if (t.equal != t.calls) std::printf("  %s, %s: %zu of %zu calls equal\n", run.car, t.name.c_str(), t.equal, t.calls);
            CHECK(t.equal == t.calls);
            if (t.name == "speed part") part = t.calls;
            if (t.name == "speed part: damping") damping = t.calls;
        }
        CHECK(part == 900);
        CHECK(!run.damping || damping > 1000);
    }
}
