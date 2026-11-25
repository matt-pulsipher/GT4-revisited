// gt4r_diff: the taps of proximity, slipstream and the speed-switched part (rules_near.h) against the program's.

#include "taps.h"

#include "gt4r/sim/rules_near.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <type_traits>
#include <vector>

namespace gt4r::diff {

namespace {

using gt3r::ee::F;
namespace rules = sim::rules;
using sim::layout::Block;

constexpr u32 kPhase = 0x3F4638, kPhaseEnd = 0x3F4B20;
constexpr u32 kCount = 0x10000 - 0x760; // conductor +: the car count
constexpr u32 kCourse = 0x10000 - 0x3428; // conductor +: the course object

F f(Ee& ee, int r) { return F::raw(ee.fpr[r]); }
u32 car_at(u32 cond, u32 i) { return cond + kCar0 + kCarSize * i; }

// A car's record as the program has it now.
sim::Record record(Ee& ee, u32 car) {
    sim::Record r;
    r.car.behaviour = rules::behaviour();
    load(r, read_image(ee, car));
    return r;
}

// The course's lap length (0x34C230: the course object's road (+0x04, then +0xDC), its +0x14).
F lap_length(Ee& ee, u32 cond) {
    const u32 course = ee.r32(cond + kCourse);
    return F::raw(ee.r32(ee.r32(ee.r32(course + 4) + 0xDC) + 0x14));
}

rules::NearPairs read_pairs(Ee& ee, u32 cond) {
    rules::NearPairs p;
    for (u32 i = 0; i < 6; ++i)
        for (u32 j = 0; j < 6; ++j) {
            p.slip[i][j] = F::raw(ee.r32(cond + rules::kSlipTable + 4 * (6 * i + j)));
            p.code[i][j] = ee.r8(cond + rules::kCodeTable + 6 * i + j);
        }
    return p;
}

// The phase: every car's record read in, ours run on all; car `t.car_index`'s record compared as a tap's, the others' fields
// of NearExtra and slipstream targets, and the pairs' tables, as the call's memory results.
void phase(TapCall& t) {
    Ee& ee = t.ee;
    const u32 cond = u32(ee.a(0));
    const int n = int(ee.r32(cond + kCount));
    std::vector<sim::Record> recs(size_t(n > 0 ? n : 0));
    std::vector<rules::NearCar> cars;
    for (int k = 0; k < n; ++k) {
        sim::Record& r = k == t.car_index ? t.rec : recs[size_t(k)];
        if (k != t.car_index) r = record(ee, car_at(cond, u32(k)));
        cars.push_back({&r.car, &r.x.near});
    }
    rules::NearPairs pairs = read_pairs(ee, cond);
    rules::proximity(cars, pairs, lap_length(ee, cond), ee.r8(cond + rules::kRuleKind));
    char name[64];
    for (int k = 0; k < n; ++k) {
        if (k == t.car_index) continue;
        const u32 car = car_at(cond, u32(k));
        rules::near_fields(recs[size_t(k)].x.near, [&](const char* field, int i, Block b, u32 off, const auto& m) {
            if (b != Block::Car || off >= sim::layout::kState) return; // (the driver's part: what the phase writes)
            std::snprintf(name, sizeof name, i < 0 ? "car%d %s" : "car%d %s[%d]", k, field, i);
            if constexpr (std::is_same_v<std::remove_cvref_t<decltype(m)>, F>) t.mem_f(name, car + off, m);
            else t.mem_i(name, car + off, u32(m), 1);
        });
        std::snprintf(name, sizeof name, "car%d slip_target", k);
        t.mem_f(name, car + kF + 0x59C, recs[size_t(k)].car.slip_target);
    }
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j) {
            std::snprintf(name, sizeof name, "slip[%d][%d]", i, j);
            t.mem_f(name, cond + rules::kSlipTable + 4 * u32(6 * i + j), pairs.slip[size_t(i)][size_t(j)]);
            std::snprintf(name, sizeof name, "code[%d][%d]", i, j);
            t.mem_i(name, cond + rules::kCodeTable + u32(6 * i + j), pairs.code[size_t(i)][size_t(j)], 1);
        }
}

// The routines' cars.
u32 car_a0(Ee& e) { return u32(e.a(0)); }
u32 car_a1(Ee& e) { return u32(e.a(1)); }
u32 driver_car(Ee& e) { return u32(e.a(0)) - sim::layout::kDriver; }
// (conductor, car index) routines: the car the index names.
u32 indexed_car(Ee& e) { return u32(e.a(1)) < 6 ? car_at(u32(e.a(0)), u32(e.a(1))) : 0; }
bool from_phase(Ee& e) {
    const u32 ra = e.g32(31);
    return ra >= kPhase && ra < kPhaseEnd;
}

rules::SpeedDampTables damp_tables(Ee& ee) {
    return rules::speed_damp_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
}

// The damping rows the session builds from the program against the race's (once).
void check_tables(Ee& ee) {
    static bool told = false;
    if (told) return;
    told = true;
    Bytes image(kBssStart - 0x100000);
    ee.read_bytes(0x100000, image.data(), u32(image.size()));
    const sim::Program program = sim::Program::from_image(std::move(image), 0x100000);
    std::printf("  speed part: the damping rows built from the program %s the race's\n",
                !program.known() ? "(no known program) against" : rules::same(rules::speed_damp_tables(program), damp_tables(ee)) ? "equal" : "differ from");
}

// GT4R_NEAR_BRANCHES: how often the calls took each branch (counted at the routines' entries, from the program's state there;
// printed when the run ends). The calls themselves are compared by the taps above.
std::map<std::string, u64>& branches() {
    static auto* m = new std::map<std::string, u64>; // (kept for the print at exit)
    return *m;
}
void print_branches() {
    std::printf("  near / speed part branches:\n");
    for (const auto& [name, n] : branches()) std::printf("      %-34s %llu\n", name.c_str(), static_cast<unsigned long long>(n));
}
u32 count_speed_part(Ee& e) {
    const u32 car = u32(e.a(0)), P = e.r32(car + 0x10);
    const u32 kind = e.r8(P + 0x1077);
    if (kind == 0) return 0;
    char n[64];
    std::snprintf(n, sizeof n, "kind %u state %u", kind, e.r8(car + kF + 0x592));
    ++branches()[n];
    if (kind == 4) std::snprintf(n, sizeof n, "kind 4 second state %u", e.r8(car + kF + 0x593)), ++branches()[n];
    return 0;
}
// The damping: its row's kind 2 below the lower speed, between, above; kind 3 on a marked surface or off one; the lift none,
// in part, whole.
u32 count_damping(Ee& e) {
    const u32 car = u32(e.a(0));
    const sim::Record r = record(e, car);
    const rules::SpeedDampTables t = damp_tables(e);
    const u8 row = r.x.body.speed_part;
    const int w = int(e.a(1));
    if (row >= 5 || t.rows[row].kind == 0) return 0;
    const rules::SpeedDampRow& k = t.rows[row];
    if (k.kind == 2) ++branches()[k.speed_hi <= r.car.speed ? "damping fast" : !(k.speed_lo < r.car.speed) ? "damping slow" : "damping shaded"];
    else if (k.kind == 3) {
        const u8 s = r.car.w[size_t(w)].surface;
        ++branches()[s < 24 && t.fixed_aim[s] != 0 ? "damping marked surface" : "damping unmarked surface"];
    }
    const ex::Suspension& s = r.car.p.susp[size_t(w >> 1)];
    const F y = (f(e, 12) - s.x_static) + rules::speed_damp_term(r.car, t, row, w);
    ++branches()[!(y < F()) ? "damping no lift" : -k.lift_travel < y ? "damping part of lift" : "damping whole lift"];
    return 0;
}
u32 count_slipstream(Ee& e) {
    const sim::Record b = record(e, u32(e.a(1)));
    ++branches()[rules::slipstream(b.car, f(e, 12), f(e, 13)) != F() ? "slipstream taken" : "slipstream none"];
    return 0;
}
u32 count_contact(Ee& e) {
    const u32 a = u32(e.a(0)), b = u32(e.a(1));
    ++branches()[e.r8(a + sim::layout::kDriver + 0x51) == e.r8(b + kF + 0x440) ? "contact on the car followed" : "contact on another"];
    return 0;
}
void add_branch_counts(Taps& taps) {
    static bool once = false;
    if (!once) once = true, std::atexit(print_branches);
    taps.add({0x3F28B8, "(branches)", count_speed_part, [](TapCall&) {}});
    taps.add({0x3F2650, "(branches)", count_damping, [](TapCall&) {}});
    taps.add({0x3F44A0, "(branches)", count_slipstream, [](TapCall&) {}});
    taps.add({0x469918, "(branches)", count_contact, [](TapCall&) {}});
}

} // namespace

void add_taps_near(Taps& taps) {
    if (std::getenv("GT4R_NEAR_BRANCHES")) add_branch_counts(taps);
    // ---- phase 14 ----
    taps.add({kPhase, "near", [](Ee& e) { return car_at(u32(e.a(0)), 0); }, phase});
    taps.add({0x343BF0, "near: left out", car_a0, [](TapCall& t) { t.ret_i("returns", rules::left_out(t.car, t.rec.x.near) ? 1 : 0); }});
    taps.add({0x3F40F8, "near: pair", car_a1, [](TapCall& t) {
                  const u32 out = u32(t.ee.a(0));
                  const sim::Record b = record(t.ee, u32(t.ee.a(2)));
                  rules::PairView pv;
                  const bool near = rules::pair_view(t.car, t.rec.x.near, b.car, b.x.near, pv);
                  t.ret_i("returns", near ? 1 : 0);
                  if (near) {
                      t.mem_f("u_a", out, pv.u_a), t.mem_f("v_a", out + 4, pv.v_a), t.mem_f("u_b", out + 8, pv.u_b), t.mem_f("v_b", out + 12, pv.v_b);
                      t.mem_f("dist", out + 16, pv.dist);
                  }
              }});
    // (its car: the phase's car i, $s1)
    taps.add({0x3F4520, "near: position code", [](Ee& e) { return from_phase(e) ? e.g32(17) : 0; }, [](TapCall& t) {
                  t.ret_i("returns", rules::position_code(f(t.ee, 12), f(t.ee, 13), f(t.ee, 14), f(t.ee, 15), f(t.ee, 16), f(t.ee, 17)));
              }});
    taps.add({0x3F44A0, "near: slipstream", car_a0, [](TapCall& t) {
                  const sim::Record b = record(t.ee, u32(t.ee.a(1)));
                  t.ret_f("returns", rules::slipstream(b.car, f(t.ee, 12), f(t.ee, 13)));
              }});
    taps.add({0x3F42D0, "near: closing", car_a0, [](TapCall& t) {
                  const sim::Record b = record(t.ee, u32(t.ee.a(1)));
                  F gap, rate;
                  const bool on = rules::closing(t.car, t.rec.x.near, b.car, b.x.near, lap_length(t.ee, t.ee.r32(u32(t.ee.a(0)) + 4)), f(t.ee, 13), gap, rate);
                  t.ret_i("returns", on ? 1 : 0);
                  t.mem_f("gap", u32(t.ee.a(2)), gap), t.mem_f("rate", u32(t.ee.a(3)), rate);
              }});
    taps.add({0x4694A0, "near: reset", driver_car, [](TapCall& t) { rules::near_reset(t.rec.x.near); }});
    taps.add({0x4694F8, "near: zones", driver_car, [](TapCall& t) {
                  rules::zones(t.rec.x.near, f(t.ee, 12), f(t.ee, 13), f(t.ee, 14), f(t.ee, 15), f(t.ee, 16), f(t.ee, 17));
              }});
    taps.add({0x469868, "near: let go", car_a0, [](TapCall& t) { rules::let_go(t.rec.x.near, record(t.ee, u32(t.ee.a(1))).car); }});
    taps.add({0x469888, "near: threat", car_a0,
              [](TapCall& t) { rules::threat(t.rec.x.near, record(t.ee, u32(t.ee.a(1))).car, f(t.ee, 14), f(t.ee, 15)); }});
    taps.add({0x469918, "near: contact", car_a0,
              [](TapCall& t) { rules::contact(t.rec.x.near, record(t.ee, u32(t.ee.a(1))).car, f(t.ee, 14), f(t.ee, 15)); }});
    // The pairs' tables and the cars' addresses (their calls from the phase).
    taps.add({0x3F4080, "near: code store", indexed_car, [](TapCall& t) {
                  t.mem_i("code", u32(t.ee.a(0)) + rules::kCodeTable + 6 * u32(t.ee.a(1)) + u32(t.ee.a(2)), u32(t.ee.a(3)) & 0xFF, 1);
              }});
    taps.add({0x3F4428, "near: slip store", indexed_car, [](TapCall& t) {
                  t.mem_f("slip", u32(t.ee.a(0)) + rules::kSlipTable + 4 * (6 * u32(t.ee.a(1)) + u32(t.ee.a(2))), f(t.ee, 12));
              }});
    taps.add({0x3F4450, "near: slip read", indexed_car, [](TapCall& t) {
                  const rules::NearPairs p = read_pairs(t.ee, u32(t.ee.a(0)));
                  t.ret_f("returns", p.slip[size_t(t.ee.a(1))][size_t(t.ee.a(2))]);
              }});
    taps.add({0x34C190, "near: car", [](Ee& e) { return from_phase(e) ? indexed_car(e) : 0; },
              [](TapCall& t) { t.ret_i("returns", car_at(u32(t.ee.a(0)), u32(t.ee.a(1)))); }});
    taps.add({0x34C230, "near: lap length",
              [](Ee& e) {
                  const u32 ra = e.g32(31);
                  return ra >= 0x3F42D0 && ra < 0x3F4428 ? car_at(u32(e.a(0)), 0) : 0;
              },
              [](TapCall& t) { t.ret_f("returns", lap_length(t.ee, u32(t.ee.a(0)))); }});

    // ---- phase 15 ----
    taps.add({0x3F28B8, "speed part", car_a0, [](TapCall& t) {
                  rules::speed_part(t.car, t.rec.x.near, t.rec.x.body.speed_part_kind, t.rec.x.chassis.speed_part_state);
              }});
    taps.add({0x3F2798, "speed part: share", car_a0, [](TapCall& t) { t.ret_f("returns", rules::speed_part_share(t.rec.x.near, f(t.ee, 12))); }});
    taps.add({0x3F2650, "speed part: damping", car_a0, [](TapCall& t) {
                  check_tables(t.ee);
                  const int w = int(t.ee.a(1));
                  t.ret_f("returns", rules::speed_damp(t.car, damp_tables(t.ee), t.rec.x.body.speed_part, t.rec.x.steer.axle_load[size_t(w >> 1)], w, f(t.ee, 12)));
              }});
    taps.add({0x3F24A0, "speed part: damping term", car_a0, [](TapCall& t) {
                  t.ret_f("returns", rules::speed_damp_term(t.car, damp_tables(t.ee), t.rec.x.body.speed_part, int(t.ee.a(1))));
              }});
}

} // namespace gt4r::diff
