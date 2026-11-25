// gt4r_diff: the taps of the suspension's spring rate and the wheels' emits (rules_wheels.h,
// rules_body.h, rules_effects.h) against the program's, and the switches that force a car's category for them.

#include "taps.h"

#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_effects.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>

namespace gt4r::diff {

namespace {

using gt3r::ee::F;
namespace rules = sim::rules;

constexpr u32 kEmitReturn = 0x3668BC, kSprayReturn = 0x3668D8; // (0x3667A8's calls of 0x395C70 / 0x395C98)

// Each car's emits handed on in the current call of 0x3668F0.
std::map<int, int>& emitted() {
    static auto* m = new std::map<int, int>;
    return *m;
}

// Our emit n of the car's (its record at the call).
bool ours(TapCall& t, int n, rules::TireEmit& e) {
    std::array<rules::TireEmit, 4> all;
    const int count = rules::tire_emits(t.car, t.rec.layout, t.rec.x.wear.category, all);
    if (n < 0 || n >= count) return false;
    e = all[size_t(n)];
    return true;
}

// The record 0x3667A8 hands on (at `at`) and the velocity (at `v`) against ours.
void compare(TapCall& t, const rules::TireEmit& e, u32 at, u32 v) {
    t.mem_i("emit wheel", at, u32(e.wheel)), t.mem_i("emit surface", at + 4, e.surface);
    for (u32 k = 0; k < 3; ++k) t.mem_f("emit point", at + 8 + 4 * k, e.point[k]);
    t.mem_f("emit heading", at + 0x14, e.heading), t.mem_f("emit attr", at + 0x18, e.attr);
    for (u32 k = 0; k < 3; ++k) t.mem_f("emit normal", at + 0x1C + 4 * k, e.normal[k]);
    t.mem_f("emit plane", at + 0x28, e.plane), t.mem_f("emit smoke", at + 0x2C, e.smoke);
    t.mem_f("emit slip ratio", at + 0x30, e.slip_ratio), t.mem_f("emit hub speed", at + 0x34, e.hub_speed);
    t.mem_f("emit rim speed", at + 0x38, e.rim_speed);
    for (u32 k = 0; k < 3; ++k) t.mem_f("emit velocity", v + 4 * k, e.velocity[k]);
}

// GT4R_PARTS_BRANCHES: the spring rates the calls gave (whole, scaled down to 0, with a sitting count) and the sprays, counted
// at the routines' entries from the program's state there; printed when the run ends.
std::map<std::string, u64>& branches() {
    static auto* m = new std::map<std::string, u64>;
    return *m;
}
void print_branches() {
    std::printf("  parts branches:\n");
    for (const auto& [name, n] : branches()) std::printf("      %-34s %llu\n", name.c_str(), static_cast<unsigned long long>(n));
}
void add_branch_counts(Taps& taps) {
    static bool once = false;
    if (!once) once = true, std::atexit(print_branches);
    taps.add({0x35D248, "(branches)", [](Ee& e) {
                  const u32 car = u32(e.a(0)), P = e.r32(car + 0x10);
                  const u8 sit = e.r8(car + 0x788);
                  if (e.r8(P + 0x32) == 1 && sit != 0) {
                      const int i = e.r8(car + sim::layout::kState + 0x440), w = int(e.a(1));
                      int m = (w + i) % 4;
                      if ((i & 7) >= 4) m = 3 - m;
                      ++branches()[sit < 7 * m ? "spring: sitting, before its turn" : sit == 7 * m ? "spring: sitting, at its turn" : "spring: sitting, past its turn"];
                  } else
                      ++branches()["spring: as the axle's"];
                  return 0u;
              },
              [](TapCall&) {}});
    taps.add({0x395C98, "(branches)", [](Ee&) { return ++branches()["spray"], 0u; }, [](TapCall&) {}});
}

} // namespace

void add_taps_parts(Taps& taps) {
    if (std::getenv("GT4R_PARTS_BRANCHES")) add_branch_counts(taps);
    // ---- phase 19: the suspension's spring rate (0x35D248, from 0x36A758 for each wheel) ----
    taps.add({0x35D248, "parts: spring rate", [](Ee& e) { return u32(e.a(0)); }, [](TapCall& t) {
                  t.ret_f("returns", rules::spring_rate(t.car, t.rec.layout, int(t.ee.a(1)), t.rec.x.wear.category, t.rec.x.phases.special[0]));
              }});
    // ---- phase 19: the wheels' emits (0x3668F0 writes nothing of the car; 0x3667A8 hands each to 0x395C70, a spray to
    // 0x395C98 too) ----
    // GT4R_STEP_SPRAY=1: each wheel's ground given the flag (+0x33 bit 5) before the call, so that every emit is a spray too.
    taps.add({0x3668F0, "parts: tire emits", [](Ee& e) {
                  const u32 car = u32(e.a(0));
                  static const char* spray = std::getenv("GT4R_STEP_SPRAY");
                  for (u32 w = 0; spray && *spray == '1' && w < 4; ++w) {
                      const u32 at = car + sim::layout::kGround0 + sim::layout::kGroundSize * w + 0x33;
                      e.w8(at, u8(e.r8(at) | 0x20));
                  }
                  return car;
              },
              [](TapCall& t) { emitted()[t.car_index] = 0; }});
    // (at 0x395C70 the car is 0x3667A8's $s0, the velocity its $s2, the record its $a1)
    taps.add({0x395C70, "parts: tire emit", [](Ee& e) { return e.g32(31) == kEmitReturn ? e.g32(16) : 0; }, [](TapCall& t) {
                  rules::TireEmit e;
                  const int n = emitted()[t.car_index]++;
                  if (!ours(t, n, e)) {
                      t.mem_i("emit beyond ours", u32(t.ee.a(1)), 0xFFFFFFFFu);
                      return;
                  }
                  compare(t, e, u32(t.ee.a(1)), t.ee.g32(18));
              }});
    if (std::getenv("GT4R_STEP_SPRAY") || std::getenv("GT4R_PARTS_BRANCHES")) // (no ground of the runs' courses has the flag)
    taps.add({0x395C98, "parts: tire spray", [](Ee& e) { return e.g32(31) == kSprayReturn ? e.g32(16) : 0; }, [](TapCall& t) {
                  rules::TireEmit e;
                  if (!ours(t, emitted()[t.car_index] - 1, e) || !e.spray) {
                      t.mem_i("spray not ours", u32(t.ee.a(1)), 0xFFFFFFFFu);
                      return;
                  }
                  compare(t, e, u32(t.ee.a(1)), u32(t.ee.a(2)));
              }});
}

void parts_before_run(Ee& ee, const OrigRace& o) {
    // GT4R_STEP_CATEGORY=k: the first car's category (P + 0x32) made k before the race's first step (no car of the disc is of
    // category 1 or 6: the pod's sitting spring rates, the paired wheels' emits).
    if (const char* k = std::getenv("GT4R_STEP_CATEGORY")) {
        ee.w8(o.params(0) + 0x32, u8(std::atoi(k)));
        std::printf("  the first car's category made %d\n", std::atoi(k));
    }
}

void parts_before_step(int step, Pad& pad) {
    // GT4R_STEP_SIT=n: the handbrake pulled (the digital third axis) for the first n steps (a pod stands and sits down).
    if (const char* n = std::getenv("GT4R_STEP_SIT"))
        if (step < std::atoi(n)) pad.third = 1, pad.flags = u8(pad.flags & ~0x10);
}

} // namespace gt4r::diff
