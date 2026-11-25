// The last of the race step's routines that ran the shared engine's code, against the user's own program: phase 16's speed
// ramp (0x345A50), the suspension with the speed-switched part's damping (0x3F2650 from 0x369DE8) and its spring rate
// (0x35D248), the wheel spin (0x344040), the wheels' emits (0x3668F0 / 0x3667A8) and the sway (0x34A668 / 0x34A788), at
// every call of the program's own race step (tools/gt4diff taps_parts.cpp, step.cpp). Reads the Gran Turismo 4 disc of
// GT4R_DISC; skips without it.

#include "test_framework.h"

#include "diff.h"
#include "orig.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_effects.h"
#include "taps.h"

#include <array>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace gt4r;

namespace {

Disc* full_disc() {
    for (const std::unique_ptr<Disc>& d : test::discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d.get();
    return nullptr;
}

// The calls of the taps named, each equal at every call; `calls` of each by name.
size_t check_taps(const diff::StepResult& r, const char* car, std::initializer_list<const char*> names, std::map<std::string, size_t>& calls) {
    size_t bad = 0;
    for (const diff::StepTap& t : r.taps)
        for (const char* n : names) {
            if (t.name != n) continue;
            calls[n] += t.calls;
            if (t.equal != t.calls) std::printf("  %s, %s: %zu of %zu calls equal\n", car, n, t.equal, t.calls), ++bad;
        }
    return bad;
}

} // namespace

TEST(sim_parts_suspension_ramp_spin_emits_equal_in_the_step) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The cars whose suspension adds the speed-switched part's term (damping rows 1, 4, 2; 3 for the MTR GMV on a loose
    // course) and an ordinary one.
    struct Run {
        const char* course;
        const char* car;
        bool damping;
    };
    for (const Run& run : {Run{"tsukuba", "sl55_amg_02", true}, Run{"tsukuba", "nike_one_04", true}, Run{"grandcanyon", "mtr_gmv_04", true},
                           Run{"tsukuba", "nsx_r_02", false}}) {
        const diff::StepResult r = diff::run_step(*disc, false, run.course, run.car, 900);
        if (!r.ran) std::printf("  %s: %s\n", run.car, r.error.c_str());
        CHECK(r.ran);
        std::map<std::string, size_t> calls;
        CHECK(check_taps(r, run.car,
                         {"suspension", "speed part: damping", "speed ramp", "wheel spin", "sway pitch", "sway roll", "parts: spring rate", "parts: tire emits",
                          "parts: tire emit", "parts: tire spray"},
                         calls) == 0);
        CHECK(calls["suspension"] == 900 && calls["speed ramp"] == 900 && calls["wheel spin"] == 900 && calls["sway roll"] == 900);
        CHECK(calls["parts: spring rate"] == 4 * 900 && calls["parts: tire emits"] == 900 && calls["parts: tire emit"] == 4 * 900);
        CHECK(!run.damping || calls["speed part: damping"] > 1000);
    }
}

TEST(sim_parts_forced_categories_equal_in_the_step) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // No car of the disc is of category 6 or 1: the first car made one before the race (P + 0x32). Category 6: an emit per
    // wheel group; category 1 (a pod) with the handbrake pulled for 150 steps: it sits down, its wheels' spring rates 0 in
    // turn, then rises.
    std::map<std::string, size_t> calls;
    ::setenv("GT4R_STEP_CATEGORY", "6", 1);
    const diff::StepResult six = diff::run_step(*disc, false, "tsukuba", "nsx_r_02", 600);
    ::setenv("GT4R_STEP_CATEGORY", "1", 1);
    ::setenv("GT4R_STEP_SIT", "150", 1);
    const diff::StepResult one = diff::run_step(*disc, false, "tsukuba", "nsx_r_02", 600);
    ::unsetenv("GT4R_STEP_CATEGORY");
    ::unsetenv("GT4R_STEP_SIT");
    CHECK(six.ran && one.ran);
    CHECK(check_taps(six, "category 6", {"parts: tire emits", "parts: tire emit", "parts: tire spray", "wheel spin", "suspension"}, calls) == 0);
    CHECK(calls["parts: tire emits"] == 600 && calls["parts: tire emit"] == 2 * 600);
    calls.clear();
    CHECK(check_taps(one, "category 1", {"parts: spring rate", "suspension", "attitude"}, calls) == 0);
    CHECK(calls["parts: spring rate"] == 4 * 600);
}

TEST(sim_parts_direct_calls_equal) {
    Disc* disc = full_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    // The program's own 0x35D248 and 0x3668F0 called on a car of a running race with what the step's runs do not meet: every
    // index of a car, sitting count and wheel layout; category 6's pairs with the one or the other wheel's ground the larger,
    // and ground with the spray's flag, the wheels' values spread.
    using gt3r::u32;
    using gt3r::u8;
    namespace layout = sim::layout;
    namespace rules = sim::rules;
    db::SpecDb sdb = diff::open_db(*disc);
    diff::Ee ee;
    diff::boot(ee, *disc, sdb);
    diff::OrigRace o;
    std::string err;
    CHECK(diff::setup_race(ee, *disc, sdb, "tsukuba", {"nsx_r_02"}, 1, o, err));
    for (int i = 0; i < 240; ++i) {
        o.pad[0] = {};
        o.pad[0].throttle = i > 150 ? 1.0f : 0.0f;
        diff::orig_step(ee, o);
    }
    const u32 car = o.car(0), P = o.params(0), F0 = car + layout::kState;
    auto ours = [&] {
        sim::Record r;
        r.car.behaviour = rules::behaviour();
        diff::load(r, diff::read_image(ee, car));
        u8 row[rules::kWheelLayoutSize];
        ee.read_bytes(rules::kWheelLayouts + rules::kWheelLayoutSize * ee.r8(P + 0x30), row, sizeof row);
        r.layout = rules::wheel_layout(row);
        return r;
    };
    const u8 row0 = ee.r8(P + 0x30);
    size_t calls = 0, equal = 0, whole = 0, none = 0;
    for (u8 row = 0; row < 4; ++row)
        for (int category : {0, 1})
            for (int index = 0; index < 8; ++index)
                for (int sit = 0; sit <= 36; ++sit) {
                    ee.w8(P + 0x30, row), ee.w8(P + 0x32, u8(category));
                    ee.w8(F0 + 0x440, u8(index)), ee.w8(car + 0x788, u8(sit));
                    const sim::Record r = ours();
                    for (int w = 0; w < 4; ++w) {
                        const u32 theirs = ee.call(0x35D248, {diff::Arg::ptr(car), diff::Arg::integer(w)}).f0;
                        const u32 mine = rules::spring_rate(r.car, r.layout, w, r.x.wear.category, r.x.phases.special[0]).bits;
                        ++calls, equal += theirs == mine;
                        (theirs == 0 ? none : whole)++;
                    }
                }
    std::printf("  spring rate: %zu calls, %zu equal (%zu whole, %zu none)\n", calls, equal, whole, none);
    CHECK(equal == calls && whole > 0 && none > 0);

    // The emits: what 0x395C70 / 0x395C98 are handed (the record's 15 words, the index or the velocity's 3).
    struct Sent {
        bool spray;
        std::array<u32, 19> w;
    };
    std::vector<Sent> sent;
    auto grab = [&](bool spray, diff::Ee& e, u32 velocity) {
        Sent s{spray, {}};
        for (u32 k = 0; k < 15; ++k) s.w[k] = e.r32(u32(e.a(1)) + 4 * k);
        for (u32 k = 0; k < 3; ++k) s.w[15 + k] = e.r32(velocity + 4 * k);
        s.w[18] = spray ? 0 : u32(e.a(2)) & 0xFF;
        sent.push_back(s);
    };
    ee.add_probe(0x395C70, [&](diff::Ee& e) { grab(false, e, e.g32(18)); });
    ee.add_probe(0x395C98, [&](diff::Ee& e) { grab(true, e, u32(e.a(2))); });
    u32 seed = 1;
    auto value = [&] { return (float(int((seed = seed * 1664525u + 1013904223u) >> 8) & 0xFFFF) / 65536.0f - 0.5f); };
    size_t runs = 0, same = 0, sprays = 0, swapped = 0, pairs = 0;
    ee.w8(car + 0x788, 0);
    for (u8 row = 0; row < 4; ++row)
        for (int category : {0, 6})
            for (int k = 0; k < 60; ++k) {
                ee.w8(P + 0x30, row), ee.w8(P + 0x32, u8(category)), ee.w8(F0 + 0x440, u8(k % 6));
                const sim::Record before = ours();
                bool paired = before.layout.groups >= 2;
                for (u32 g = 0; g < 2; ++g) paired = paired && before.layout.partner[before.layout.group_first[g] & 7] != 0x7F;
                if (category == 6 && !paired) continue; // (a wheel without a partner: the program reads past the records)
                for (u32 w = 0; w < 4; ++w) {
                    const u32 W = car + layout::kWheel0 + layout::kWheelSize * w, G = car + layout::kGround0 + layout::kGroundSize * w;
                    for (u32 at : {0x00u, 0x0Cu, 0x10u, 0x20u, 0x28u, 0x38u, 0x8Cu, 0xBCu}) ee.wf(W + at, value());
                    // (each third run every wheel's the same: the tie)
                    ee.wf(G + 0x14, k % 3 == 2 ? 0.25f : value());
                    for (u32 at : {0x1Cu, 0x20u, 0x24u, 0x28u}) ee.wf(G + at, value());
                    ee.w8(G + 0x33, value() > 0 ? 0x21 : 0x01);
                }
                const sim::Record r = ours();
                sent.clear();
                ee.call(0x3668F0, {diff::Arg::ptr(car)});
                std::array<rules::TireEmit, 4> e;
                const int n = rules::tire_emits(r.car, r.layout, r.x.wear.category, e);
                std::vector<Sent> mine;
                for (int i = 0; i < n; ++i) {
                    const rules::TireEmit& t = e[size_t(i)];
                    Sent s{false,
                           {u32(t.wheel), t.surface, t.point[0].bits, t.point[1].bits, t.point[2].bits, t.heading.bits, t.attr.bits, t.normal[0].bits,
                            t.normal[1].bits, t.normal[2].bits, t.plane.bits, t.smoke.bits, t.slip_ratio.bits, t.hub_speed.bits, t.rim_speed.bits,
                            t.velocity[0].bits, t.velocity[1].bits, t.velocity[2].bits, t.car}};
                    mine.push_back(s);
                    if (t.spray) s.spray = true, s.w[18] = 0, mine.push_back(s), ++sprays;
                    if (category == 6) ++pairs, swapped += t.wheel != int(r.layout.group_first[size_t(i)]);
                }
                bool ok = sent.size() == mine.size();
                for (size_t i = 0; ok && i < mine.size(); ++i) ok = sent[i].spray == mine[i].spray && sent[i].w == mine[i].w;
                ++runs, same += ok;
            }
    ee.w8(P + 0x30, row0);
    std::printf("  emits: %zu calls, %zu equal (%zu sprays; %zu pairs, %zu by the partner)\n", runs, same, sprays, pairs, swapped);
    CHECK(same == runs && sprays > 0 && pairs > swapped && swapped > 0);
}
