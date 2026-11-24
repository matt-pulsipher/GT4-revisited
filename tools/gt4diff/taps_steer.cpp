// gt4r_diff's taps of the steering phase (0x3F7EA8) and its routines (taps.h, sim::rules steer).

#include "taps.h"

#include "gt4r/sim/rules_steer.h"

#include <cstdio>
#include <cstdlib>
#include <map>

namespace gt4r::diff {

namespace {

using gt3r::ee::F;
namespace rules = sim::rules;

constexpr u32 kPhase = 0x3F7EA8, kPhaseEnd = 0x3F8460;

u32 car_a0(Ee& e) { return u32(e.a(0)); }
// A routine the phase calls: its car is the phase's ($s4), for the calls from the phase only.
u32 phase_car(Ee& e) {
    const u32 ra = e.g32(31);
    return ra >= kPhase && ra < kPhaseEnd ? e.g32(20) : 0;
}

rules::SteerTables steer_tables(Ee& ee) {
    return rules::steer_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
}

F f(Ee& ee, int r) { return F::raw(ee.fpr[r]); }

// The tables the session builds from the program against the ones the race's start left in memory (once).
void check_tables(Ee& ee) {
    static bool told = false;
    if (told) return;
    told = true;
    Bytes image(kBssStart - 0x100000);
    ee.read_bytes(0x100000, image.data(), u32(image.size()));
    const sim::Program program = sim::Program::from_image(std::move(image), 0x100000);
    const rules::SteerTables mine = rules::steer_tables(program), theirs = steer_tables(ee);
    std::printf("  steer: the tables built from the program %s the race's (mode %u, scale %u, compliance %u)\n",
                !program.known() ? "(no known program) against" : rules::same(mine, theirs) ? "equal" : "differ from", theirs.mode, theirs.scale,
                theirs.compliance_on);
}

// GT4R_STEER_VARY (bits): made-up settings written into the program's memory before each phase call, to run the
// branches a pad car on Tsukuba leaves out. 1: steering mode 2; 2: the offset's scale 1, 2, 3 in turn by step; 4: the
// surfaces 0 .. 3 marked in 0x620288 over steps 300 .. 700; 8: the rear wheels steer (P + 0x10D0 1, + 0x10CC 0.3);
// 16: the two-wheeler's layout row 1 over the phase only (P + 0x30, put back at phase 8), the lean row by step; 32: the
// car's age F + 0x5E8 from 0 to 4,000,000 over the run; 64: the compliance's limit and dead band small (0x620B04, the
// dead band's y), the offset's limits small (0x845BF8).
u32 vary(Ee& ee, u32 mask) {
    const u32 car = u32(ee.a(0)), P = ee.r32(car + 0x10), F_ = car + kF;
    static std::map<u32, int> calls;
    const int n = calls[car]++;
    if (mask & 1) ee.w32(rules::kSteerSettings, 2);
    if (mask & 2) ee.w32(rules::kSteerSettings + 4, u32(1 + n % 3));
    if (mask & 4)
        for (u32 s = 0; s < 4; ++s) ee.w8(rules::kFixedAim + s, n >= 300 && n < 700 ? 1 : 0);
    if (mask & 8) ee.w8(P + 0x10D0, 1), ee.wf(P + 0x10CC, 0.3f);
    if (mask & 16) ee.w8(P + 0x30, 1), ee.w8(P + 0x33, u8(n % 4));
    if (mask & 32) ee.wf(F_ + 0x5E8, 4000000.0f * float(n) / 1200.0f);
    if (mask & 64) {
        ee.wf(rules::kCompliance + 4, 0.002f);
        ee.wf(ee.r32(rules::kCompliance + 0x40 + 8), 50.0f), ee.wf(ee.r32(rules::kCompliance + 0x40 + 8) + 4, 300.0f);
        ee.wf(rules::kSteerSettings + 8, 0.0005f), ee.wf(rules::kSteerSettings + 12, 0.0002f);
    }
    return 0; // (no comparison of its own)
}

// The layout row put back (the cars run here have row 0) at phase 8 (slip ratios, 0x351778) after GT4R_STEER_VARY's 16.
u32 unvary(Ee& ee) {
    const u32 car = u32(ee.a(0)), P = ee.r32(car + 0x10);
    if (ee.r8(P + 0x30) == 1) ee.w8(P + 0x30, 0), ee.w8(P + 0x33, 0);
    return 0;
}

} // namespace

void add_taps_steer(Taps& taps) {
    if (const char* v = std::getenv("GT4R_STEER_VARY")) {
        const u32 mask = u32(std::atoi(v));
        taps.add({kPhase, "steer: settings", [mask](Ee& e) { return vary(e, mask); }, [](TapCall&) {}});
        if (mask & 16) taps.add({0x351778, "steer: settings back", unvary, [](TapCall&) {}});
    }
    taps.add({kPhase, "steer", car_a0, [](TapCall& t) {
                  check_tables(t.ee);
                  rules::wheel_steer(t.car, t.rec.x.steer, t.rec.x.tires, t.rec.layout, steer_tables(t.ee));
              }});
    // (its car: the phase's, whose parameter block is the routine's first argument)
    taps.add({0x3F7CA8, "steer: wheel angle",
              [](Ee& e) {
                  const u32 car = phase_car(e);
                  return car != 0 && e.r32(car + 0x10) == u32(e.a(0)) ? car : 0;
              },
              [](TapCall& t) { t.ret_f("returns", rules::wheel_angle(t.car, t.rec.x.steer, t.rec.layout, int(t.ee.a(1)), f(t.ee, 12))); }});
    taps.add({0x3F7E20, "steer: lean angle", car_a0, [](TapCall& t) {
                  t.ret_f("returns", rules::lean_angle(t.car, t.rec.x.steer, t.rec.layout, steer_tables(t.ee), int(t.ee.a(1))));
              }});
    taps.add({0x40CC58, "steer: follower", car_a0, [](TapCall& t) { rules::follow_steer(t.car, t.rec.x.steer); }});
    taps.add({0x36CC40, "steer: angle difference", phase_car, [](TapCall& t) { t.ret_f("returns", rules::angle_diff(f(t.ee, 12), f(t.ee, 13))); }});
    taps.add({0x36CCD8, "steer: angle blend", phase_car,
              [](TapCall& t) { t.ret_f("returns", rules::angle_blend(f(t.ee, 12), f(t.ee, 13), f(t.ee, 14), f(t.ee, 15))); }});
    taps.add({0x359568, "steer: table lookup", phase_car, [](TapCall& t) {
                  const u32 at = u32(t.ee.a(0));
                  ex::Table<8> tab;
                  tab.n = gt3r::s16(t.ee.r16(at));
                  for (u32 k = 0; k < 8 && k < u32(tab.n); ++k)
                      tab.x[k] = F::raw(t.ee.r32(t.ee.r32(at + 4) + 4 * k)), tab.y[k] = F::raw(t.ee.r32(t.ee.r32(at + 8) + 4 * k));
                  t.ret_f("returns", ex::lookup_bisect(tab, f(t.ee, 12)));
              }});
}

} // namespace gt4r::diff
