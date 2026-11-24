// gt4r_diff's taps of the driving aids' phase (0x3F79A0) and its routines (taps.h, sim::rules aids).

#include "taps.h"

#include "gt4r/sim/rules_aids.h"

#include <cstdio>
#include <cstdlib>
#include <map>

namespace gt4r::diff {

namespace {

using gt3r::ee::F;
using gt3r::s8;
namespace rules = sim::rules;

u32 car_a0(Ee& e) { return u32(e.a(0)); }

rules::AidTables aid_tables(Ee& ee) {
    return rules::aid_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
}

// An aid block in the interpreter's memory.
ex::Aids read_aids(Ee& ee, u32 at) {
    ex::Aids a;
    auto f = [&](u32 o) { return F::raw(ee.r32(at + o)); };
    a.a_gain = f(0x00), a.a_thr = f(0x04), a.d_gain = f(0x08), a.c_gain = f(0x10), a.c_thr = f(0x14), a.e_gain = f(0x18);
    a.tcs_gain = f(0x20), a.tcs_lat_a = f(0x24), a.tcs_lat_b = f(0x28), a.tcs_scale = f(0x2C);
    for (u32 k = 0; k < 2; ++k) {
        a.a_wheels[k] = ee.r8(at + 0x0C + k), a.d_wheels[k] = ee.r8(at + 0x0E + k);
        a.c_wheels[k] = ee.r8(at + 0x1C + k), a.e_wheels[k] = ee.r8(at + 0x1E + k);
    }
    for (u32 k = 0; k < 4; ++k) a.on[k] = ee.r8(at + 0x30 + k);
    return a;
}

// The computer driver's aid block of the car at `car` (0x463BE0 on its driver object, car + 0x14).
u32 driver_block_at(Ee& ee, u32 car) {
    const u32 drv = car + sim::layout::kDriver;
    const u32 pb = ee.r32(0x10000 + 4 * u32(ee.r8(ee.r32(drv + 0xE8) + 0x544)) + ee.r32(drv + 0xE4) + 0x24);
    return pb + 0x24;
}

// What the phase reads of the race, from the car's conductor.
rules::AidRace aid_race(Ee& ee, u32 car) {
    rules::AidRace r;
    const u32 cond = ee.r32(car + 4), rule = cond + 0xF818;
    const u8 kind = ee.r8(cond + 0x10000 - 0x3423);
    r.course_kind = kind == 2 ? 1 : kind == 3 ? 2 : 0;
    r.speed_cap = {ee.r8(rule + 0x42), ee.r8(rule + 0x43)};
    r.start_cap = ee.r8(rule + 0x4B), r.fuel_cap = ee.r8(rule + 0x4C);
    r.start_speed = ee.r8(rule + 0x6E);
    if (r.start_speed == 0) {
        // The conductor's virtual +0x30: the single-player conductor's (0x358248) answers 0.
        const u32 vf = ee.r32(ee.r32(cond + kCondVtable) + 0x30 + 4);
        r.start_speed = vf == 0x358248 ? ee.r8(cond + 0x10000 - 0x79A + ee.r8(car + kF + 0x440)) : ee.r8(rule + 0x4E);
    }
    const u32 course = ee.r32(cond + kCondCourse);
    r.lap_length = F::raw(ee.r32(ee.r32(ee.r32(course + 4) + 0xDC) + 0x14));
    return r;
}

// The brakes' shares in the interpreter's memory (0x3F92D0's 0x40 bytes).
rules::BrakeShares read_shares(Ee& ee, u32 at) {
    rules::BrakeShares s;
    for (u32 k = 0; k < 2; ++k) s.axle_brake[k] = F::raw(ee.r32(at + 4 * k)), s.axle_cap[k] = F::raw(ee.r32(at + 8 + 4 * k));
    for (u32 k = 0; k < 4; ++k) {
        s.cap_mul[k] = F::raw(ee.r32(at + 0x10 + 4 * k)), s.brake_mul[k] = F::raw(ee.r32(at + 0x20 + 4 * k));
        s.brake_add[k] = F::raw(ee.r32(at + 0x30 + 4 * k));
    }
    return s;
}
void expect_shares(TapCall& t, u32 at, const rules::BrakeShares& s) {
    for (u32 k = 0; k < 2; ++k) t.mem_f("S.axle_brake", at + 4 * k, s.axle_brake[k]), t.mem_f("S.axle_cap", at + 8 + 4 * k, s.axle_cap[k]);
    for (u32 k = 0; k < 4; ++k) {
        t.mem_f("S.cap_mul", at + 0x10 + 4 * k, s.cap_mul[k]), t.mem_f("S.brake_mul", at + 0x20 + 4 * k, s.brake_mul[k]);
        t.mem_f("S.brake_add", at + 0x30 + 4 * k, s.brake_add[k]);
    }
}

// Curve D's table of a tire record (n, x, y) in the interpreter's memory, and the lookup's value (f12).
template <class Fn> F slip_lookup(Ee& ee, Fn&& fn) {
    const u32 t = u32(ee.a(0));
    const s16 n = s16(ee.r16(t));
    F x[32], y[32];
    for (int k = 0; k < n && k < 32; ++k) x[k] = F::raw(ee.r32(ee.r32(t + 4) + 4 * u32(k))), y[k] = F::raw(ee.r32(ee.r32(t + 8) + 4 * u32(k)));
    return fn(n, x, y, F::raw(ee.fpr[12]));
}


// GT4R_AIDS_VARY (bits): made-up settings written into the program's memory before each phase call, to run the
// branches a pad car on Tsukuba leaves out. 1: a car's own block with every channel and TCS on; 2: the static
// switches (TCS mode 0, the ABS target's blends, the indicators' scales and followers, the ASCC cut's gain, car
// + 0x55); 4: the race's caps (120 km/h, the start's 60 km/h for 10 s, out of fuel from step 700, the pit limit
// over steps 900 .. 960), the cornering hold and the ASCC marks; 8: a course of kind 2 (the dirt cut); 16: the
// ASCC switches 0x6224A6 .. A8 turned (0, 0, 1); 32: the ASCC switches (1, 0, 1); 64: driver kind 1 and no laps
// over steps 300 .. 700 and a rolling start's speed of 80 km/h.
u32 vary(Ee& ee, u32 mask) {
    const u32 car = u32(ee.a(0)), F_ = car + kF, P = ee.r32(car + 0x10), cond = ee.r32(car + 4);
    static std::map<u32, int> calls;
    const int n = calls[car]++;
    if (mask & 1) {
        const float fl[] = {1.5f, 0.05f, 2.0f, 0, 2.5f, 0.02f, 1.8f, 0, 3.0f, 2.0f, 0.3f, 0.8f};
        const u32 at = P + 0x1098;
        const u32 fo[] = {0x00, 0x04, 0x08, 0, 0x10, 0x14, 0x18, 0, 0x20, 0x24, 0x28, 0x2C};
        for (int k = 0; k < 12; ++k)
            if (k != 3 && k != 7) ee.wf(at + fo[k], fl[k]);
        const u8 wh[] = {1, 1, 1, 0, 1, 0, 1, 1};
        for (u32 k = 0; k < 4; ++k) ee.w8(at + 0x0C + k, wh[k]), ee.w8(at + 0x1C + k, wh[4 + k]), ee.w8(at + 0x30 + k, 1);
    }
    if (mask & 2) {
        ee.w8(0x620DEF, 0), ee.w8(0x620DED, 50), ee.w8(0x620DEE, 30);
        for (u32 k = 0; k < 4; ++k) ee.w8(0x620E0C + k, 1);
        ee.wf(0x6202A0, 0.5f), ee.wf(0x6202A4, 1.2f), ee.wf(0x6202A8, 0.8f);
        ee.w8(car + 0x55, 1), ee.w8(car + 0x81, 3);
    }
    if (mask & 4) {
        ee.w8(cond + 0xF818 + 0x42, 120), ee.w8(cond + 0xF818 + 0x4B, 60), ee.w8(cond + 0xF818 + 0x4C, 30);
        if (n == 0) ee.w16(F_ + 0x60E, 600);
        ee.wf(F_ + 0x4CC, 50.0f), ee.wf(F_ + 0x4D0, n >= 700 ? 0.0f : 20.0f);
        ee.w8(F_ + 0x5C3, n >= 900 && n < 960 ? 1 : 0);
        ee.w8(F_ + 0x4E5, ee.r8(F_ + 0x4E5) | 2), ee.w8(P + 0x32, 2);
    }
    if (mask & 8) ee.w8(cond + 0x10000 - 0x3423, 2);
    if (mask & 16) ee.w8(0x6224A6, 0), ee.w8(0x6224A7, 0), ee.w8(0x6224A8, 1);
    if (mask & 32) ee.w8(0x6224A6, 1), ee.w8(0x6224A7, 0), ee.w8(0x6224A8, 1);
    if (mask & 64) { // driver kind 1 over steps 300 .. 700, with a rolling start's speed of 80 km/h for the car
        ee.w8(F_ + 0x467, n >= 300 && n < 700 ? 1 : 0);
        if (n >= 300 && n < 700) ee.w16(F_ + 0x4AC, 0);
        ee.w8(cond + 0x10000 - 0x79A + ee.r8(F_ + 0x440), 80);
    }
    return 0; // (no comparison of its own)
}

} // namespace

void add_taps_aids(Taps& taps) {
    if (const char* v = std::getenv("GT4R_AIDS_VARY")) {
        const u32 mask = u32(std::atoi(v));
        taps.add({0x3F79A0, "aids: settings", [mask](Ee& e) { return vary(e, mask); }, [](TapCall&) {}});
    }
    // The race tables the session builds against the program's after its set-up (the TCS classes made by 0x3F1790).
    taps.add({0x3F79A0, "aids", car_a0, [](TapCall& t) {
                  static bool told = false;
                  const rules::AidTables g = aid_tables(t.ee);
                  if (!told) {
                      told = true;
                      s8 b[5];
                      for (u32 k = 0; k < 5; ++k) b[k] = s8(t.ee.r8(rules::kTcsClassBytes + k));
                      const std::array<F, 5> made = rules::tcs_classes(b);
                      bool same = true;
                      for (size_t k = 0; k < 5; ++k) same = same && made[k].bits == g.tcs_class[k].bits;
                      std::printf("  aids: the TCS classes made from 0x6220A0 %s the program's\n", same ? "equal" : "differ from");
                      // Curve D's table (tire record +0x170) has curve C's points (+0xF8) and axis (+0x104).
                      const u32 P = t.ee.r32(u32(t.ee.a(0)) + 0x10);
                      bool table_same = true;
                      for (u32 r = 0; r < 6; ++r) {
                          const u32 R = P + rules::kTire0 + rules::kTireSize * r;
                          table_same = table_same && t.ee.r16(R + 0x170) == t.ee.r16(R + 0xF8) && t.ee.r32(R + 0x174) == R + 0x104 &&
                                       t.ee.r32(R + 0x178) == R + 0x17C;
                      }
                      std::printf("  aids: curve D's tables %s curve C's points and axis\n", table_same ? "have" : "do not have");
                  }
                  const u32 drv = driver_block_at(t.ee, u32(t.ee.a(0)));
                  const ex::Aids d = read_aids(t.ee, drv);
                  rules::aids(t.car, t.rec.x.aids, t.rec.x.tires, t.rec.x.drive, t.rec.layout, g, aid_race(t.ee, u32(t.ee.a(0))), &d);
              }});
    taps.add({0x3F71E8, "aids: indicators", car_a0,
              [](TapCall& t) { rules::indicators(t.car, t.rec.x.aids, t.rec.x.tires, t.rec.layout, aid_tables(t.ee)); }});
    taps.add({0x3F9D00, "aids: TCS", car_a0, [](TapCall& t) {
                  rules::tcs(t.car, t.rec.x.aids, t.rec.x.tires, t.rec.x.drive, t.rec.layout, aid_tables(t.ee), read_aids(t.ee, u32(t.ee.a(1))));
              }});
    taps.add({0x3F9870, "aids: brakes", car_a0, [](TapCall& t) {
                  const u32 car = u32(t.ee.a(0));
                  rules::brakes(t.car, t.rec.x.aids, t.rec.x.tires, t.rec.layout, aid_tables(t.ee), aid_race(t.ee, car), read_aids(t.ee, u32(t.ee.a(1))),
                                t.ee.a(2) != 0);
              }});
    taps.add({0x3F94C0, "aids: ASCC", car_a0, [](TapCall& t) {
                  const u32 at = u32(t.ee.a(2));
                  rules::BrakeShares s = read_shares(t.ee, at);
                  rules::ascc(t.car, t.rec.x.aids, read_aids(t.ee, u32(t.ee.a(1))), s, t.ee.a(3) != 0, aid_tables(t.ee));
                  expect_shares(t, at, s);
              }});
    taps.add({0x3F9340, "aids: brake shares", car_a0, [](TapCall& t) {
                  const u32 at = u32(t.ee.a(2));
                  std::array<F, 4> cap;
                  for (u32 k = 0; k < 4; ++k) cap[k] = F::raw(t.ee.r32(at + 4 * k));
                  rules::apply_shares(t.car, t.rec.layout, read_shares(t.ee, u32(t.ee.a(1))), cap);
                  for (int w = 0; w < t.rec.layout.count && w < 4; ++w) t.mem_f("cap", at + 4 * u32(w), cap[size_t(w)]);
              }});
    taps.add({0x3F76E8, "aids: hold throttle", car_a0, [](TapCall& t) {
                  t.ret_f("returns", rules::hold_throttle(t.car, t.rec.x.aids, t.rec.x.drive, F::raw(t.ee.fpr[12])));
              }});
    taps.add({0x3F7880, "aids: start pace", car_a0, [](TapCall& t) {
                  u32 speed = 0;
                  t.ret_i("returns", u32(rules::start_pace(t.car, aid_race(t.ee, u32(t.ee.a(0))), speed)));
                  if (t.ee.a(1) != 0 && t.ee.r32(u32(t.ee.a(1))) == 0) t.mem_i("speed", u32(t.ee.a(1)), speed);
              }});
    // The curve D lookups (their callers keep the car in $s6).
    taps.add({0x3596C0, "aids: rising slip", [](Ee& e) { return e.g32(22); },
              [](TapCall& t) { t.ret_f("returns", slip_lookup(t.ee, rules::rising_slip)); }});
    taps.add({0x359778, "aids: falling slip", [](Ee& e) { return e.g32(22); },
              [](TapCall& t) { t.ret_f("returns", slip_lookup(t.ee, rules::falling_slip)); }});
}

} // namespace gt4r::diff
