// gt4r_diff: the tap of the steering wheel's force (0x353EE8, rules_force.h) against the program's, with the pad block's
// bytes and the tables the race's start built.

#include "taps.h"

#include "gt4r/sim/rules_force.h"
#include "gt4r/sim/rules_steer.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <vector>

namespace gt4r::diff {

namespace {

using gt3r::ee::F;
using gt3r::s16;
namespace rules = sim::rules;

constexpr u32 kForce = 0x353EE8, kForceReturn = 0x34456C; // (the phase's instruction after the call)
constexpr u32 kCourseKind = 0x10000 - 0x3423;
constexpr u32 kRaceAverage = 0x318; // the race object's virtual the LAN battle's mean answers
// Places in the program's start-up code nothing of a race runs, for the variations' stand-ins: the controller's wheel
// options (its virtual +0x20 of the table at controller + 0xD0) and a race object's virtual answering 1.
constexpr u32 kStubOptions = 0x10000C, kStubOne = 0x100010;

u32 car_a0(Ee& e) { return u32(e.a(0)); }
u32 cond_of(Ee& ee, u32 car) { return ee.r32(car + 4); }
u32 pad_block(Ee& ee, u32 car) { return cond_of(ee, car) + rules::kPadBlock + rules::kPadSize * ee.r8(car + kF + 0x440); }
u32 controller_vt(Ee& ee, u32 car) { return ee.r32(ee.r32(car + 0xC) + 0xD0); }
u32 race_vt(Ee& ee, u32 car) { return ee.r32(ee.r32(ee.r32(car) + 0x84) + 0x64); }

// Calls compared: within the bounds, zero, at a bound, a computer's car (GT4R_FORCE_COVER prints them at the end).
u64 g_seen[4] = {};

rules::ForceTables force_tables(Ee& ee) {
    return rules::read_force_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
}
rules::SteerTables steer_tables(Ee& ee) {
    return rules::steer_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
}

s16 g_strength = 1, g_turn = 1; // what kStubOptions answers

rules::ForceRace force_race(Ee& ee, u32 car) {
    rules::ForceRace r;
    const u8 k = ee.r8(cond_of(ee, car) + kCourseKind);
    r.loose = k == 2 || k == 3; // (0x34C998: 0xFF reads 0; 2 → 1, 3 → 2)
    r.lan_average = ee.r32(race_vt(ee, car) + kRaceAverage + 4) == kStubOne;
    if (ee.r32(controller_vt(ee, car) + 0x20 + 4) == kStubOptions) r.strength = g_strength, r.turn_factor = g_turn;
    return r;
}

void print_mode(int m, const rules::ForceMode& k) {
    std::printf("    mode %d: align %g, gain %g / %g, curves %u (%g %g %g) / %u, turn %g, steer %g (max %g), offset %g / %g (curve %u), slip %u"
                " (%g %g %g %g), limit kind %u, strength %g %g %g, wheel %u %u %u, rate x %g %g %g y %g %g %g, +0x64 %g %g %g %g %g %u\n",
                m, double(k.align_gain.host()), double(k.gain[0].host()), double(k.gain[1].host()), k.curve[0].kind, double(k.curve[0].a.host()),
                double(k.curve[0].b.host()), double(k.curve[0].c.host()), k.curve[1].kind, double(k.turn_gain.host()), double(k.steer_gain.host()),
                double(k.steer_max.host()), double(k.offset_gain.host()), double(k.offset_damp.host()), k.offset_curve.kind, k.slip_on,
                double(k.slip_base.host()), double(k.slip_share.host()), double(k.slip_inv.host()), double(k.drive_share.host()), k.limit_kind,
                double(k.strength[0].host()), double(k.strength[1].host()), double(k.strength[2].host()), k.wheel[0], k.wheel[1], k.wheel[2],
                double(k.rate_x[0].host()), double(k.rate_x[1].host()), double(k.rate_x[2].host()), double(k.rate_y[0].host()),
                double(k.rate_y[1].host()), double(k.rate_y[2].host()), double(k.x64.host()), double(k.x68.host()), double(k.x6c.host()),
                double(k.x70.host()), double(k.x74.host()), k.x78);
}

// The tables built from the disc's program against the ones the race's start left in memory (once, before any variation),
// and with GT4R_FORCE_STATE=<eeMemory.bin> against a saved state of the original's.
u32 check_tables(Ee& ee) {
    static bool told = false;
    if (told) return 0;
    told = true;
    OrigRace* race = active_race();
    if (!race || !race->disc) return 0;
    const sim::Program program = sim::Program::load(*race->disc);
    const rules::ForceTables mine = rules::force_tables(program), theirs = force_tables(ee);
    std::string why;
    const bool eq = rules::same(mine, theirs, &why);
    std::printf("  force: the tables built from the program %s the race's%s%s (offset switch %u)\n", eq ? "equal" : "differ from", eq ? "" : ": ",
                why.c_str(), theirs.offset_feel);
    for (int m = 0; m < 3; ++m) print_mode(m, theirs.mode[size_t(m)]);
    if (const char* state = std::getenv("GT4R_FORCE_STATE")) {
        FILE* fp = std::fopen(state, "rb");
        if (!fp) return 0;
        std::vector<u8> mem(32u << 20);
        const size_t n = std::fread(mem.data(), 1, mem.size(), fp);
        std::fclose(fp);
        auto rd = [&](u32 a) { return a + 4 <= n ? u32(mem[a] | (mem[a + 1] << 8) | (mem[a + 2] << 16) | (u32(mem[a + 3]) << 24)) : 0u; };
        auto rb = [&](u32 a) { return a < n ? mem[a] : u8(0); };
        const rules::ForceTables real = rules::read_force_tables(rd, rb);
        const bool eq2 = rules::same(mine, real, &why);
        std::printf("  force: the tables built from the program %s the saved state's%s%s\n", eq2 ? "equal" : "differ from", eq2 ? "" : ": ",
                    eq2 ? "" : why.c_str());
        for (u32 a = rules::kForceTables; a < rules::kForceTables + 3 * rules::kForceModeSize; a += 4)
            if (ee.r32(a) != rd(a)) std::printf("    %06X: the race's %08X, the state's %08X\n", a, ee.r32(a), rd(a));
    }
    return 0;
}

// ---- GT4R_FORCE_VARY ---------------------------------------------------------------------------------------------

// The variations (GT4R_FORCE_VARY=<n>): made-up settings written before each call of a car's (its calls counted n), put
// back after the call (at the phase's next instruction; the start's release count and the pad block's bytes are left as
// the call leaves them). 1 the controller kinds (F + 0x465 0 .. 3, P + 0x10D3, the course's kind, the wheel's options, the
// frame's steering, the command, the LAN mean, the release count); 2 a computer's car (control sources 1 / 2, driver kinds,
// the frame's steering out to the byte's ends, kind 3's doubling); 3 the tables' switches (each mode's limit kind 0 .. 3,
// the slip shares on, the curves' kinds, gains); 4 the steering offset's term (the steering mode 0x845BF0 1 / 2, the switch
// 0x622173 0 .. 2, the front wheels' offsets and rates, slow and fast); 5 slow driving (below 2 km/h, the command past 5
// degrees) with kinds 0 and 3, the offset's switch 0 and 2.
struct Saved {
    u32 at, size;
    u8 bytes[8];
};
std::vector<Saved> g_saved;
void save(Ee& ee, u32 at, u32 size) {
    Saved s{at, size, {}};
    ee.read_bytes(at, s.bytes, size);
    g_saved.push_back(s);
}
void put8(Ee& ee, u32 at, u8 v) { save(ee, at, 1), ee.w8(at, v); }
void put16(Ee& ee, u32 at, u16 v) { save(ee, at, 2), ee.w16(at, v); }
void put32(Ee& ee, u32 at, u32 v) { save(ee, at, 4), ee.w32(at, v); }
void putf(Ee& ee, u32 at, float v) { save(ee, at, 4), ee.wf(at, v); }
void restore(Ee& ee) {
    for (auto it = g_saved.rbegin(); it != g_saved.rend(); ++it)
        for (u32 k = 0; k < it->size; ++k) ee.w8(it->at + k, it->bytes[k]);
    g_saved.clear();
}

float wave(int n, int period) { return float((n % period) - period / 2) / float(period / 2); }

u32 vary(Ee& ee, int mode) {
    check_tables(ee); // (before anything is changed)
    static bool skipped = false;
    static std::map<u32, int> calls;
    if (!ee.has_hle(kStubOptions)) { // (a fresh interpreter: a new run)
        // The put-back runs after the tap's own comparison at the call's return, which the run's first call installs.
        if (!skipped) {
            skipped = true, calls.clear(), g_saved.clear();
            return 0;
        }
        ee.add_probe(kForceReturn, [](Ee& e) { restore(e); });
        ee.hle(kStubOptions, [](Ee& e) { e.w16(u32(e.a(1)), u16(g_strength)), e.w16(u32(e.a(1)) + 2, u16(g_turn)); }, "wheel options");
        ee.hle(kStubOne, [](Ee& e) { e.set_v0(1); }, "race object virtual (1)");
        skipped = false;
    }
    const u32 car = u32(ee.a(0)), P = ee.r32(car + 0x10), F_ = car + kF, cond = cond_of(ee, car);
    const int n = calls[car]++;
    const u32 vt = controller_vt(ee, car), rvt = race_vt(ee, car), pad = pad_block(ee, car);
    // (the pad block's bytes the force clears, filled)
    for (u32 o = 8; o < 0x10; ++o) ee.w8(pad + o, u8(0x50 + o));
    auto frame_steer = [&](s16 now, s16 last, s16 seen_last) {
        put16(ee, F_ + 0x454, u16(now)), put16(ee, F_ + 0x456, u16(last)), put16(ee, F_ + 0x45A, u16(seen_last));
    };
    switch (mode) {
    case 1: { // the controller kinds
        put8(ee, F_ + 0x465, u8((n / 40) % 4));
        put8(ee, P + 0x10D3, u8((n / 100) % 2));
        put8(ee, cond + kCourseKind, u8((n / 75) % 5 == 4 ? 0xFF : (n / 75) % 5));
        put32(ee, vt + 0x20 + 4, kStubOptions), g_strength = s16((n / 30) % 3), g_turn = s16((n / 90) % 2);
        if ((n / 200) % 2) put32(ee, rvt + kRaceAverage + 4, kStubOne);
        const s16 now = s16(wave(n, 50) * 3000.0f), last = s16(wave(n + 3 + (n / 13) % 5, 50) * 3000.0f);
        frame_steer(now, (n / 7) % 3 == 0 ? now : last, s16(-last / 2));
        putf(ee, F_ + 0x468, wave(n, 64) * 0.3f);
        if (n % 120 == 5) ee.w8(F_ + 0x633, 30);
        if (n % 120 == 60) ee.w8(F_ + 0x633, 3);
        break;
    }
    case 2: { // a computer's car
        put8(ee, F_ + 0x466, u8(n % 3 == 0 ? 1 : n % 3 == 1 ? 2 : 0));
        put8(ee, F_ + 0x467, u8(n % 3 == 2 ? ((n / 3) % 2 ? 3 : 0) : 0));
        put8(ee, F_ + 0x465, u8((n / 5) % 2 ? 3 : (n / 10) % 3));
        const s16 s = (n / 4) % 6 == 0 ? s16(4100 * 2) : (n / 4) % 6 == 1 ? s16(-8200) : s16(wave(n, 40) * 4096.0f);
        frame_steer(s, s16(s / 2), 0);
        if (n % 50 == 7) ee.w8(F_ + 0x633, 12);
        break;
    }
    case 3: { // the tables' switches
        for (u32 m = 0; m < 3; ++m) {
            const u32 T = rules::kForceTables + rules::kForceModeSize * m;
            put8(ee, T + 0x7C, u8((n / 30 + m) % 4));
            if ((n / 60) % 2) {
                put8(ee, T + 0x50, 1);
                putf(ee, T + 0x54, 0.6f), putf(ee, T + 0x58, 0.4f), putf(ee, T + 0x5C, 5.0f), putf(ee, T + 0x60, (n / 120) % 2 ? 3.0f : -2.5f);
            }
            put32(ee, T + 0xC, u32((n / 45 + m) % 4)), put32(ee, T + 0x1C, u32((n / 55 + m) % 4));
            putf(ee, T + 0x10, -0.3f), putf(ee, T + 0x14, 0.2f), putf(ee, T + 0x18, 1.2f);
            putf(ee, T + 0x20, -0.1f), putf(ee, T + 0x24, 0.3f), putf(ee, T + 0x28, 0.8f);
            putf(ee, T + 4, (n / 25) % 2 ? 0.4f : 1.6f), putf(ee, T + 8, 0.7f);
        }
        put8(ee, F_ + 0x465, u8((n / 20) % 4));
        put8(ee, P + 0x10D3, u8((n / 70) % 2));
        put8(ee, cond + kCourseKind, u8((n / 90) % 4));
        put32(ee, vt + 0x20 + 4, kStubOptions), g_strength = s16((n / 15) % 3), g_turn = 1;
        break;
    }
    case 4: { // the steering offset's term
        put32(ee, rules::kSteerSettings, u32(1 + (n / 50) % 2));
        put8(ee, rules::kOffsetFeel, u8((n / 150) % 3));
        for (u32 w = 0; w < 4; ++w) {
            const u32 W = car + sim::layout::kWheel0 + sim::layout::kWheelSize * w;
            putf(ee, W + 0x1C, wave(n + int(w) * 7, 36) * 0.05f), putf(ee, W + 0x18, wave(n + 11, 22) * 0.4f);
        }
        if ((n / 25) % 2) putf(ee, F_ + 0x544, 0.1f), putf(ee, F_ + 0x548, -0.2f);
        put8(ee, F_ + 0x465, u8((n / 100) % 2 ? 3 : 1));
        put8(ee, P + 0x10D3, u8((n / 200) % 2));
        break;
    }
    case 5: { // slow driving
        putf(ee, F_ + 0x544, (n / 20) % 2 ? 0.3f : 0.6f), putf(ee, F_ + 0x548, 0.1f);
        putf(ee, F_ + 0x468, wave(n, 30) * 0.2f);
        put8(ee, F_ + 0x465, u8((n / 60) % 2 ? 3 : 0));
        put32(ee, rules::kSteerSettings, 1), put8(ee, rules::kOffsetFeel, (n / 120) % 2 ? 2 : 0); // (the race's switch 1: slow is the offset's)
        break;
    }
    default:
        break;
    }
    return 0;
}

// GT4R_FORCE_COVER=<file>: the routine's instructions the program ran, appended to the file at the end (the variations' runs
// together give the instructions no run reached).
std::map<u32, u64> g_hits;
void write_cover() {
    std::printf("  force: %llu calls within the bounds, %llu zero, %llu at +-1, %llu of a computer's car\n", static_cast<unsigned long long>(g_seen[0]),
                static_cast<unsigned long long>(g_seen[1]), static_cast<unsigned long long>(g_seen[2]), static_cast<unsigned long long>(g_seen[3]));
    const char* path = std::getenv("GT4R_FORCE_COVER");
    FILE* fp = path ? std::fopen(path, "a") : nullptr;
    if (!fp) return;
    for (const auto& [a, n] : g_hits) std::fprintf(fp, "%06X %llu\n", a, static_cast<unsigned long long>(n));
    std::fclose(fp);
}
u32 install_cover(Ee& ee) {
    static bool done = false;
    if (done) return 0;
    done = true;
    for (u32 a = kForce; a < 0x354A74; a += 4) g_hits[a] = 0, ee.add_probe(a, [a](Ee&) { ++g_hits[a]; });
    std::atexit(write_cover);
    return 0;
}

} // namespace

void add_taps_force(Taps& taps) {
    if (std::getenv("GT4R_FORCE_COVER")) taps.add({kForce, "(force: cover)", install_cover, [](TapCall&) {}});
    if (const char* v = std::getenv("GT4R_FORCE_VARY")) {
        const int mode = std::atoi(v);
        taps.add({kForce, "(force: vary)", [mode](Ee& e) { return vary(e, mode); }, [](TapCall&) {}});
    }
    taps.add({kForce, "steer force", [](Ee& e) { check_tables(e); return car_a0(e); }, [](TapCall& t) {
                  Ee& ee = t.ee;
                  const u32 car = t.race.car(t.car_index), b = pad_block(ee, car);
                  rules::ForceTables k = force_tables(ee);
                  const rules::SteerTables g = steer_tables(ee);
                  rules::steer_force(t.car, t.rec.x, t.rec.layout, k, g, force_race(ee, car));
                  t.mem_i("pad force", b + 2, t.rec.x.force.out, 1);
                  // A player's car: the force x 127 whole, which the program still holds in $f0 at the return.
                  const bool computer = u8(ee.r8(car + kF + 0x466) - 1) < 2 || ee.r8(car + kF + 0x467) != 0;
                  if (!computer) {
                      t.ret_f("force x 127", t.rec.x.force.scaled);
                      const float v = t.rec.x.force.scaled.host();
                      ++g_seen[v == 0 ? 1 : v >= 127.0f || v <= -127.0f ? 2 : 0];
                  } else
                      ++g_seen[3];
                  t.mem_i("pad +0x08", b + 8, 0, 2), t.mem_i("pad +0x0A", b + 0xA, 0, 2);
                  for (u32 j = 0; j < 3; ++j) t.mem_i("pad wheel bytes", b + 0xD + j, t.rec.x.force.wheel[j], 1);
              }});
}

} // namespace gt4r::diff
