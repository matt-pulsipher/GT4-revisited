// gt4r_diff: the taps of the driver's input (rules_input.h) against the program's.

#include "taps.h"

#include "gt4r/formats/course/course_file.h"
#include "gt4r/formats/course/runway.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/rules_input.h"
#include "gt4r/sim/rules_steer.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>

namespace gt4r::diff {

namespace {

using gt3r::ee::F;
using gt3r::s8;
using gt3r::s16;
namespace rules = sim::rules;

constexpr u32 kPhase = 0x349FD0, kPhaseEnd = 0x34A4B8, kPhaseReturn = 0x34DE60; // (the step's instruction after the phase's return)
constexpr u32 kSteer = 0x348850, kSteerEnd = 0x349418, kControlsEnd = 0x349F44;
constexpr u32 kCourseKind = 0x10000 - 0x3423, kCourseRoad = 0x10000 - 0x3422;

F f(Ee& ee, int r) { return F::raw(ee.fpr[r]); }
u32 car_a0(Ee& e) { return u32(e.a(0)); }
u32 car_state(Ee& e) { return u32(e.a(0)) - kF; }
u32 cond_of(Ee& ee, u32 car) { return ee.r32(car + 4); }
bool pad_car(Ee& ee, u32 car) { return ee.r8(car + kF + 0x466) == 0 && ee.r8(car + kF + 0x467) == 0; }

rules::PadTables pad_tables(Ee& ee) {
    return rules::read_pad_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
}
rules::SteerTables steer_tables(Ee& ee) {
    return rules::steer_tables([&](u32 a) { return ee.r32(a); }, [&](u32 a) { return ee.r8(a); });
}
rules::PadFrame frame_at(Ee& ee, u32 at) {
    u8 b[0x20];
    ee.read_bytes(at, b, 0x20);
    return rules::PadFrame::from_bytes(b);
}
// Two places in the program's code nothing of a race runs, for GT4R_INPUT_VARY's race object virtuals that answer 1.
constexpr u32 kStubOne = 0x100008;
constexpr u32 kRaceGain = 0x290, kRaceAverage = 0x318; // the race object's virtuals the prefilter asks

u32 race_vtable(Ee& ee, u32 car) { return ee.r32(ee.r32(ee.r32(car) + 0x84) + 0x64); }

rules::InputRace input_race(Ee& ee, u32 car) {
    rules::InputRace r;
    const u8 k = ee.r8(cond_of(ee, car) + kCourseKind);
    r.course_kind = k == 0xFF ? 0 : k; // 0x34C968
    r.line_heading = F::raw(0);         // (the harness's stand-in for 0x4649F0)
    const u32 vt = race_vtable(ee, car);
    r.lan_gain = ee.r32(vt + kRaceGain + 4) == kStubOne, r.lan_average = ee.r32(vt + kRaceAverage + 4) == kStubOne;
    return r;
}

// The course's road as our session reads it, for 0x350C20's query (built once per course).
const sim::RoadGround* road(const OrigRace& race) {
    static std::string label;
    static std::unique_ptr<course::Runway> runway;
    static std::unique_ptr<sim::RoadGround> ground;
    if (!race.disc) return nullptr;
    if (!ground || label != race.course_label) {
        const gt3r::Volume::Entry* file = race.disc->volume().find("crs/" + race.course_label);
        if (!file) return nullptr;
        const Bytes data = race.disc->volume().read(*file);
        const course::CourseFile parsed = course::CourseFile::parse(data);
        runway = std::make_unique<course::Runway>(course::Runway::parse(parsed.section(data, course::kSlotRunway)));
        ground = std::make_unique<sim::RoadGround>(*runway);
        label = race.course_label;
    }
    return ground.get();
}

rules::InputHooks hooks(TapCall& t) {
    rules::InputHooks h;
    const sim::RoadGround* g = road(t.race);
    if (g)
        h.ground = [g](const F p[3]) {
            rules::GroundAnswer a;
            const F q[3] = {p[0], p[1] + F::raw(0x40A00000), p[2]}; // (0x395760: from 5 m above)
            sim::GroundHit hit;
            if (!g->query(q, hit)) return a;
            a.found = true;
            a.height = F(hit.point.y);
            a.triangle = (u32(u16(hit.cluster)) << 16) | u16(hit.triangle);
            return a;
        };
    // 0x343C20 answers 0 (open) at once when F + 0x4B0 is set (the variations set it for a pit command); otherwise ours
    // cannot say (the race's lap rules): counted as shut.
    const u32 car = t.race.car(t.car_index);
    Ee& ee = t.ee;
    h.no_pit = [&ee, car]() { return ee.r8(car + kF + 0x4B0) == 0; };
    return h;
}

// The tables built from the disc's program against the ones the race's start left in memory (once), and with
// GT4R_INPUT_STATE=<eeMemory.bin> against a saved state of the original's.
void check_tables(TapCall& t) {
    static bool told = false;
    if (told) return;
    told = true;
    Ee& ee = t.ee;
    if (!t.race.disc) return;
    const sim::Program program = sim::Program::load(*t.race.disc);
    const u32 cond = cond_of(ee, t.race.car(t.car_index));
    const rules::PadTables mine = rules::pad_tables(program, ee.r8(cond + kCourseRoad)), theirs = pad_tables(ee);
    std::string why;
    const bool eq = rules::same(mine, theirs, &why);
    std::printf("  input: the tables built from the program %s the race's%s%s (course road byte %u)\n", eq ? "equal" : "differ from",
                eq ? "" : ": ", why.c_str(), ee.r8(cond + kCourseRoad));
    if (const char* state = std::getenv("GT4R_INPUT_STATE")) {
        FILE* fp = std::fopen(state, "rb");
        if (!fp) return;
        std::vector<u8> m(32u << 20);
        const size_t n = std::fread(m.data(), 1, m.size(), fp);
        std::fclose(fp);
        auto rd = [&](u32 a) { return a + 4 <= n ? u32(m[a] | (m[a + 1] << 8) | (m[a + 2] << 16) | (u32(m[a + 3]) << 24)) : 0u; };
        auto rb = [&](u32 a) { return a < n ? m[a] : u8(0); };
        const rules::PadTables real = rules::read_pad_tables(rd, rb);
        const rules::PadTables built = rules::pad_tables(program, 0);
        const bool eq2 = rules::same(built, real, &why);
        if (!eq2)
            std::printf("  input: least locks %08X %08X %08X %08X / the state's %08X %08X %08X %08X\n", built.least_lock_a[0].bits, built.least_lock_a[1].bits,
                        built.least_lock_b[0].bits, built.least_lock_b[1].bits, real.least_lock_a[0].bits, real.least_lock_a[1].bits, real.least_lock_b[0].bits,
                        real.least_lock_b[1].bits);
        std::printf("  input: the tables built from the program %s the saved state's%s%s\n", eq2 ? "equal" : "differ from", eq2 ? "" : ": ",
                    eq2 ? "" : why.c_str());
        // the words that differ, the race's (as built) against the state's
        const u32 ranges[][2] = {{0x620248, 0x620288}, {0x843538, 0x843630}, {0x6220A8, 0x6220E0}};
        for (const auto& r : ranges)
            for (u32 a = r[0]; a < r[1]; a += 4)
                if (ee.r32(a) != rd(a)) std::printf("    %06X: the race's %08X, the state's %08X\n", a, ee.r32(a), rd(a));
    }
}

// ---- GT4R_INPUT_VARY ---------------------------------------------------------------------------------------------

// The variations (GT4R_INPUT_VARY=<n>): made-up frames and settings written before each phase call (its steps' windows
// by the step count n of the car's calls), put back after the phase. 1 digital axes (kind 0) with the back and third
// axes, shifts; 2 a kind 2 stick; 3 the pedal styles 0 .. 6 in turn with the back and third axes analog; 4 a kind 4 wheel
// with the wheel types -1 .. 5, made-up pedal curves (kinds 1 .. 3) and the slip aid; 5 a kind 6 wheel with P + 0x10D3
// 0 / 1, the prefilter's orders 1 / 2, a loose course kind; 6 the settings' switches: the analog slew, the lock's
// extensions 1 / 2, the active steering (on 1 / 2, modes 0 .. 2), the surfaces marked loose; 7 the nitrous; 8 the pit
// answers and B-spec commands; 9 the two-wheeler's row; 10 categories 4 and 6 with the clutch shifting; 11 the gearbox
// kinds 4, 3 and 1 over the phase; 12 slow driving and reversing (the one-speed creep, the stepless creep); 13 the race
// object's virtuals the prefilter asks (a LAN battle's: the gain + 2, the mean of two frames); 14 made-up motion (sliding,
// countersteering, the heading off the line) with the lock's extensions and the active steering.
struct Saved {
    u32 at, size;
    u8 bytes[16];
};
std::vector<Saved> g_saved;
void save(Ee& ee, u32 at, u32 size) {
    Saved s{at, size, {}};
    ee.read_bytes(at, s.bytes, size);
    g_saved.push_back(s);
}
void put8(Ee& ee, u32 at, u8 v) { save(ee, at, 1), ee.w8(at, v); }
void put32(Ee& ee, u32 at, u32 v) { save(ee, at, 4), ee.w32(at, v); }
void putf(Ee& ee, u32 at, float v) { save(ee, at, 4), ee.wf(at, v); }

void restore(Ee& ee) {
    for (auto it = g_saved.rbegin(); it != g_saved.rend(); ++it)
        for (u32 k = 0; k < it->size; ++k) ee.w8(it->at + k, it->bytes[k]);
    g_saved.clear();
}

float wave(int n, int period) { return float((n % period) - period / 2) / float(period / 2); }

u32 vary(Ee& ee, int mode) {
    const u32 car = u32(ee.a(0));
    OrigRace* race = active_race();
    if (!race) return 0;
    int index = -1;
    for (int i = 0; i < race->cars; ++i)
        if (race->car(i) == car) index = i;
    if (index < 0 || !pad_car(ee, car)) return 0;
    static std::map<u32, int> calls;
    const int n = calls[car]++;
    Pad& p = race->pad[index];
    const u32 F_ = car + kF, P = ee.r32(car + 0x10), cond = cond_of(ee, car);
    p.raw = false;
    p.buttons = {};
    p.nitrous = 0;
    switch (mode) {
    case 1: // digital
        p.kind = 0, p.flags = 0;
        p.steer = (n / 40) % 3 == 0 ? 0.0f : (n / 40) % 3 == 1 ? 1.0f : -1.0f;
        p.brake = n > 900 || (n / 25) % 7 == 3 ? 1.0f : 0.0f;
        p.third = (n / 30) % 9 == 4 ? 1.0f : 0.0f;
        if (n > 1000) p.back = (n / 20) % 2 ? 1.0f : 0.0f, p.throttle = (n / 50) % 2 ? 1.0f : 0.0f;
        p.shift = n % 97 == 50 ? 1 : n % 97 == 80 ? -1 : 0;
        if (n % 300 >= 220 && n % 300 < 240) p.flags = 2 | 8, p.throttle = 0, p.back = 0; // (analog axes at 0 while a digital rise holds)
        break;
    case 2: // a kind 2 stick
        p.kind = 2, p.flags = 1 | 2 | 4 | 8 | 0x10;
        p.steer = wave(n, 120) * 0.8f, p.third = (n / 50) % 5 == 2 ? 0.7f : 0.0f;
        break;
    case 3: { // the pedal styles with the back and third axes
        p.kind = 3, p.flags = u8(1 | 2 | 4 | ((n / 150) % 2 ? 8 | 0x10 : 0));
        put8(ee, F_ + 0x442, u8((n / 60) % 8));
        p.back = (n / 35) % 3 == 1 ? 0.6f : 0.0f;
        if ((n / 35) % 4 == 2) p.throttle = 0.3f;
        p.third = (n / 45) % 4 == 3 ? 0.9f : 0.0f;
        p.steer = wave(n, 90) * 0.5f;
        p.shift = n % 61 == 30 ? 1 : n % 61 == 45 ? -1 : 0;
        break;
    }
    case 4: { // a kind 4 wheel
        p.kind = 4, p.flags = u8(n % 200 < 150 ? 1 | 2 | 4 | 8 | 0x10 : 0);
        p.steer = wave(n, 70) * 0.9f;
        put8(ee, F_ + 0x443, u8(s8((n / 50) % 7 - 1)));
        for (u32 k = 0; k < 3; ++k) {
            const u32 at = rules::kPadTables + 0x10 * k;
            const u32 kind = u32((n / 100 + k) % 5);
            put32(ee, at, kind);
            putf(ee, at + 4, kind == 1 ? -0.4f : kind == 2 ? -0.05f : 0.0f), putf(ee, at + 8, kind == 3 ? 0.4f : 0.1f), putf(ee, at + 0xC, 1.3f);
        }
        put8(ee, F_ + 0x4E5, u8((n / 120) % 2 ? 0x18 : 0));
        if ((n / 25) % 3 == 1) put8(ee, cond + kCourseKind, u8((n / 75) % 6));
        break;
    }
    case 5: { // a kind 6 wheel, the prefilter
        p.kind = 6, p.flags = 1 | 2 | 4;
        p.steer = wave(n, 80) * 0.6f;
        put8(ee, P + 0x10D3, u8((n / 100) % 2));
        for (u32 r = 0; r < 3; ++r) put8(ee, rules::kPadSettings + 0x8A + 0x20 * r, u8((n / 150) % 3)), put8(ee, rules::kPadSettings + 0x8B + 0x20 * r, 2);
        if ((n / 200) % 3 == 2) put8(ee, cond + kCourseKind, u8(2 + (n / 50) % 2));
        put8(ee, F_ + 0x4E5, u8((n / 130) % 2 ? 0x08 : 0x10));
        break;
    }
    case 6: { // the settings' switches
        p.kind = (n / 300) % 2 ? 3 : 2, p.flags = u8(n % 400 < 300 ? 1 | 2 | 4 : 2 | 4);
        p.steer = wave(n, 100) * 0.7f;
        if ((n / 100) % 4 == 1) p.steer = 0;
        put8(ee, rules::kPadSettings + 0x11, u8((n / 75) % 2));
        put8(ee, rules::kPadSettings + 0x26, u8((n / 110) % 3)), put8(ee, rules::kPadSettings + 0x22, u8((n / 170) % 3));
        put32(ee, 0x84357C, u32((n / 90) % 3)), put32(ee, 0x84358C, u32((n / 270) % 3));
        put8(ee, car + 0x32, u8((n / 45) % 2));
        for (u32 s = 0; s < 24; ++s) put8(ee, rules::kFixedAim + s, u8((n / 230) % 2 ? 1 : ee.r8(rules::kFixedAim + s)));
        put8(ee, F_ + 0x4E5, u8((n / 160) % 2 ? 0x10 : 0));
        if ((n / 60) % 5 == 4) put32(ee, 0x8435A8, 0), put32(ee, 0x8435AC, 0); // (the follower without the slip's pull)
        if ((n / 80) % 2) putf(ee, 0x843590, 6.2831855f);                        // (the active steering's window a whole turn)
        if ((n / 140) % 2) putf(ee, 0x8435F8, 12.0f), putf(ee, 0x8435FC, 12.0f), putf(ee, 0x8435F4, 2.0f); // (the follower's rates by speed)
        put8(ee, rules::kPadSettings + 0x8A, u8((n / 130) % 3)), put8(ee, rules::kPadSettings + 0x8B, u8((n / 50) % 3));
        break;
    }
    case 7: // the nitrous
        p.nitrous = (n / 30) % 2 ? 1 : 0;
        putf(ee, P + 0x10D4, (n / 200) % 2 ? 0.5f : 0.0f), putf(ee, F_ + 0x4E0, (n / 100) % 3 ? 10.0f : 0.0f);
        break;
    case 8: { // pit answers and B-spec commands, the mailbox, a ghost, the line's point off the road
        const int k = n % 24, round = n / 24;
        const u32 idx = ee.r8(F_ + 0x440);
        if (k == 1) {
            const bool none = round % 3 == 2;
            p.buttons[0] = 1, p.buttons[1] = u8(none || round % 2 ? 0xFF : 3), p.buttons[2] = u8(none ? 0xFF : 2), p.buttons[3] = u8(none ? 0 : 1);
            put32(ee, F_ + 0x4B8, (ee.r32(F_ + 0x4B8) & 0xFFFF0000) | u32(0x1C | (round % 2 ? 0 : 0x100)));
        } else if (k == 3) {
            p.buttons[0] = 1;
            put32(ee, F_ + 0x4B8, (ee.r32(F_ + 0x4B8) & 0xFFFF0000) | u32(round % 2 ? 0x08 : 0)); // (not waiting)
        } else if (k == 5) {
            p.buttons[0] = 2;
            put32(ee, F_ + 0x4B8, (ee.r32(F_ + 0x4B8) & 0xFFFF0000) | u32(round % 3 == 0 ? 0 : round % 3 == 1 ? 0x100 : 0x140));
        } else if (k == 7) {
            p.buttons[4] = u8(round % 3 == 0 ? 1 : round % 3 == 1 ? 7 : 11), p.buttons[5] = 1; // (kinds without a command)
        } else if (k == 9) {
            p.buttons[4] = 4, p.buttons[5] = u8(round % 5);
            if (round % 2) putf(ee, F_ + 0x4CC, 60.0f);
        } else if (k == 13) {
            p.buttons[4] = 5, p.buttons[5] = u8(round % 3);
            put8(ee, car + 0x30, 0); // (the line 0: no line change)
        } else if (k == 17) {
            p.buttons[4] = 6, p.buttons[5] = u8(round % 2);
            put8(ee, F_ + 0x4B0, 1);
            save(ee, F_ + 0x4AC, 2), ee.w16(F_ + 0x4AC, u16((round / 2) % 2));
            put8(ee, F_ + 0x4B1, u8((ee.r8(F_ + 0x4B1) & ~0x0C) | ((round / 4) % 4) * 4));
        } else if (k == 21) {
            p.buttons[4] = 10, p.buttons[5] = u8(round % 2);
            put8(ee, F_ + 0x466, 0), put8(ee, car + 0x54, 0), put8(ee, car + 0x55, 1); // (put back after the phase)
        } else if (k == 11) { // the mailbox (the request the controller is given; the harness's controller ignores it)
            put8(ee, cond + 0xF910 + 4 * idx, 1), put8(ee, cond + 0xF910 + 4 * idx + 1, 2);
            put8(ee, cond + 0xF8F8 + 4 * idx, 4), put8(ee, cond + 0xF8F8 + 4 * idx + 1, 1);
        } else if (k == 15) {
            put8(ee, F_ + 0x441, 1); // (a ghost: the conductor's virtual 25 left out)
        } else if (k == 19) {
            putf(ee, F_ + 0x61C, 1.0e5f), putf(ee, F_ + 0x620, 1.0e5f); // (the line's point off the road)
        }
        break;
    }
    case 9: // the two-wheeler's row
        put8(ee, P + 0x30, 1), put8(ee, P + 0x33, u8(n % 4));
        p.steer = (n / 40) % 4 == 0 ? wave(n, 60) * 0.8f : (n / 40) % 4 == 1 ? 1.0f : (n / 40) % 4 == 2 ? -1.0f : 0.0f;
        if ((n / 20) % 3) putf(ee, F_ + 0x480, ((n % 7) - 3) * 2.0f);   // (the steering rate, phase 19's)
        if ((n / 30) % 4 == 3) putf(ee, F_ + 0x660, ((n % 5) - 2) * 40.0f), putf(ee, F_ + 0x65C, ((n % 3) - 1) * 1.5f);
        if ((n / 25) % 5 == 4) putf(ee, F_ + 0x480, ((n % 2) ? 1.0f : -1.0f) * 60.0f);
        if ((n / 35) % 4 == 2) putf(ee, car + sim::layout::kWheel0 + 0x34, ((n % 2) ? 1.0f : -1.0f) * 2.5f); // (wheel 0's slip angle)
        if ((n / 45) % 5 == 3) putf(ee, F_ + 0x468, ((n % 2) ? 1.0f : -1.0f) * 1.5f); // (a command past the lock)
        break;
    case 10: // categories 4 and 6
        put8(ee, P + 0x32, (n / 100) % 2 ? 6 : 4);
        if ((n / 7) % 3 == 0) put8(ee, F_ + 0x522, 3);
        p.brake = (n / 33) % 3 == 1 ? 0.5f : 0.0f;
        break;
    case 11: // the gearbox's kinds over the phase
        put8(ee, P + 0x246, u8((n / 80) % 4 == 0 ? 4 : (n / 80) % 4 == 1 ? 3 : (n / 80) % 4 == 2 ? 1 : 0));
        p.brake = (n / 40) % 3 == 1 ? 0.6f : 0.0f;
        p.third = (n / 55) % 3 == 1 ? 1.0f : 0.0f, p.flags = 1 | 2 | 4 | 0x10;
        if ((n / 13) % 5 == 2) put8(ee, F_ + 0x4BC, 1);
        if ((n / 17) % 4 == 1) save(ee, F_ + 0x4FA, 2), ee.w16(F_ + 0x4FA, u16(s16(-3000)));
        break;
    case 12: // slow and backwards, on the one-speed and the stepless boxes too
        p.throttle = (n / 60) % 3 == 0 ? 0.0f : 0.4f, p.brake = (n / 60) % 3 == 2 ? 0.3f : 0.0f;
        p.back = (n / 120) % 2 ? 0.5f : 0.0f, p.flags = 1 | 2 | 4 | 8;
        if (p.back > 0) p.throttle = 0;
        if ((n / 240) % 3) put8(ee, P + 0x246, (n / 240) % 3 == 1 ? 3 : 1);
        break;
    case 13: { // the race object's virtuals: the prefilter's LAN gain and mean
        static bool stub = false;
        if (!stub) ee.hle(kStubOne, [](Ee& e) { e.set_v0(1); }, "race object virtual (1)"), stub = true;
        const u32 vt = race_vtable(ee, car);
        if ((n / 100) % 2) put32(ee, vt + kRaceGain + 4, kStubOne);
        if ((n / 200) % 2) put32(ee, vt + kRaceAverage + 4, kStubOne);
        for (u32 r = 0; r < 3; ++r) put8(ee, rules::kPadSettings + 0x8A + 0x20 * r, u8(1 + (n / 50) % 2)), put8(ee, rules::kPadSettings + 0x8B + 0x20 * r, 1);
        p.steer = wave(n, 40) * 0.9f;
        break;
    }
    case 14: { // made-up motion over the phase: the front wheels' slip direction past the lock, the yaw rate and its
               // follower of either sign, the heading, with the lock's extensions and the active steering on
        p.kind = (n / 200) % 3 == 2 ? 0 : 3, p.flags = p.kind == 0 ? 0 : 1 | 2 | 4;
        p.steer = (n / 15) % 3 == 0 ? 0.0f : (n / 15) % 3 == 1 ? 0.6f : -0.6f;
        const float sgn = (n / 7) % 2 ? 1.0f : -1.0f;
        for (u32 w = 0; w < 2; ++w) putf(ee, car + sim::layout::kWheel0 + sim::layout::kWheelSize * w + 0x30, sgn * (0.2f + 0.15f * float(n % 9)));
        putf(ee, F_ + 0x57C, ((n / 3) % 2 ? 1.0f : -1.0f) * 0.8f), putf(ee, F_ + 0x63C, ((n / 5) % 2 ? 1.0f : -1.0f) * 2.0f);
        putf(ee, F_ + 0x570, float((n % 11) - 5) * 0.05f);
        putf(ee, F_ + 0x540, float((n % 13) - 6) * 4.0f); // (the sideways speed)
        put8(ee, rules::kPadSettings + 0x26, u8(1 + (n / 50) % 2)), put8(ee, rules::kPadSettings + 0x22, u8(1 + (n / 100) % 2));
        put32(ee, 0x84357C, 2), put32(ee, 0x84358C, u32((n / 70) % 3)), putf(ee, 0x843590, 0.15f);
        if (ee.rf(F_ + 0x53C) < 10.0f) putf(ee, F_ + 0x53C, 15.0f);
        break;
    }
    default:
        break;
    }
    if (std::getenv("GT4R_INPUT_DEBUG"))
        std::printf("  vary %d: F+0x464 %u, pedal %g, flags %u, throttle %g, hold %u/%u\n", n, ee.r8(F_ + 0x464), double(ee.rf(F_ + 0x488)), p.flags, double(p.throttle),
                    ee.r16(F_ + 0x4BA), ee.r8(F_ + 0x4BC));
    return 0;
}

// The tap at the phase: the program's frame is the pad stub's from the race's pad.
void phase(TapCall& t) {
    check_tables(t);
    const rules::PadTables tab = pad_tables(t.ee);
    u8 b[0x20];
    pad_frame(t.race.pad[t.car_index], b);
    rules::driver_input(t.car, t.rec.x, t.rec.layout, tab, steer_tables(t.ee), input_race(t.ee, t.race.car(t.car_index)), hooks(t),
                        rules::PadFrame::from_bytes(b));
}

// A routine called by the phase or its own: its car is the caller's (the register the caller keeps it in).
u32 caller_car(Ee& e) {
    const u32 ra = e.g32(31);
    if (ra >= kSteer && ra < kSteerEnd) return e.g32(19);       // 0x348850: $s3
    if (ra >= kSteerEnd && ra < kControlsEnd) return e.g32(19); // 0x349418: $s3
    if (ra >= kPhase && ra < kPhaseEnd) return e.g32(18);       // 0x349FD0: $s2
    return 0;
}

// GT4R_INPUT_COVER=<file>: the instructions of the phase's routines the program ran, appended to the file at the end (the
// variations' runs together give the branches no run reached: ~/gt4re-out/sim-step/input/cover.py).
const u32 kCovered[][2] = {{0x349FD0, 0x34A4B8}, {0x349F48, 0x349F80}, {0x349418, 0x349F44}, {0x348850, 0x349418}, {0x3485D0, 0x3487F0},
                           {0x3484D0, 0x348584}, {0x3481D8, 0x348364}, {0x348368, 0x3484D0}, {0x35BFE8, 0x35C0B8}, {0x3F19E0, 0x3F1BD8},
                           {0x350C20, 0x350CE8}, {0x35A9D0, 0x35AB04}};
std::map<u32, u64> g_hits;
void write_cover() {
    const char* path = std::getenv("GT4R_INPUT_COVER");
    if (!path) return;
    FILE* fp = std::fopen(path, "a");
    if (!fp) return;
    for (const auto& [a, n] : g_hits) std::fprintf(fp, "%06X %llu\n", a, static_cast<unsigned long long>(n));
    std::fclose(fp);
}
u32 install_cover(Ee& ee) {
    static bool done = false;
    if (done) return 0;
    done = true;
    for (const auto& r : kCovered)
        for (u32 a = r[0]; a < r[1]; a += 4) g_hits[a] = 0, ee.add_probe(a, [a](Ee&) { ++g_hits[a]; });
    std::atexit(write_cover);
    return 0;
}

} // namespace

void add_taps_input(Taps& taps) {
    if (std::getenv("GT4R_INPUT_COVER")) taps.add({kPhase, "(input: cover)", install_cover, [](TapCall&) {}});
    if (const char* v = std::getenv("GT4R_INPUT_VARY")) {
        const int mode = std::atoi(v);
        taps.add({kPhase, "(input: vary)", [mode](Ee& e) { return vary(e, mode); }, [](TapCall&) {}});
        taps.add({kPhaseReturn, "(input: vary back)",
                  [](Ee& e) {
                      restore(e);
                      return 0u;
                  },
                  [](TapCall&) {}});
    }
    // (a pad car; not a call whose frame hands the car to its B-spec driver (command 10, 1): the program then runs the
    // computer's path, which the session's code is, not these routines; the command itself is the "b-spec" tap's)
    auto pad = [](Ee& e) {
        const u32 car = u32(e.a(0));
        const OrigRace* race = active_race();
        for (int i = 0; race && i < race->cars; ++i)
            if (race->car(i) == car && race->pad[i].buttons[4] == 10 && race->pad[i].buttons[5] != 0) return 0u;
        return pad_car(e, car) ? car : 0u;
    };
    taps.add({kPhase, "input", pad, phase});
    taps.add({0x349F48, "input: pad", car_a0, [](TapCall& t) {
                  rules::pad_input(t.car, t.rec.x, t.rec.layout, pad_tables(t.ee), steer_tables(t.ee), input_race(t.ee, t.race.car(t.car_index)),
                                   frame_at(t.ee, u32(t.ee.a(1))));
              }});
    taps.add({0x349418, "input: controls", car_a0, [](TapCall& t) {
                  rules::pad_controls(t.car, t.rec.x, t.rec.layout, pad_tables(t.ee), steer_tables(t.ee), input_race(t.ee, t.race.car(t.car_index)),
                                      frame_at(t.ee, u32(t.ee.a(1))));
              }});
    taps.add({kSteer, "input: steering", car_a0, [](TapCall& t) {
                  rules::steer_command(t.car, t.rec.x, t.rec.layout, pad_tables(t.ee), steer_tables(t.ee), input_race(t.ee, t.race.car(t.car_index)),
                                       frame_at(t.ee, u32(t.ee.a(1))));
              }});
    taps.add({0x3485D0, "input: follower", car_a0, [](TapCall& t) {
                  rules::steer_follow(t.car, t.rec.x.input, pad_tables(t.ee), int(t.ee.a(1)), int(t.ee.a(2)), f(t.ee, 12), f(t.ee, 13));
              }});
    taps.add({0x3484D0, "input: hud", car_state, [](TapCall& t) { rules::pad_hud(t.car, frame_at(t.ee, u32(t.ee.a(1)))); }});
    taps.add({0x3481D8, "input: clutch", car_a0,
              [](TapCall& t) { rules::pad_clutch(t.car, frame_at(t.ee, u32(t.ee.a(1))), u8(t.ee.a(2))); }});
    taps.add({0x348368, "input: one-speed clutch", car_state, [](TapCall& t) { rules::one_speed_clutch(t.car); }});
    taps.add({0x35BFE8, "input: pit answer", car_a0, [](TapCall& t) {
                  rules::PadFrame fr;
                  t.ee.read_bytes(u32(t.ee.a(1)), fr.buttons.data(), 16);
                  rules::pit_answer(t.car, t.rec.x, fr);
              }});
    taps.add({0x3F19E0, "input: b-spec", car_a0, [](TapCall& t) {
                  rules::bspec_command(t.car, t.rec.x, pad_tables(t.ee), hooks(t), u8(t.ee.a(2)), u8(t.ee.a(3)));
              }});
    taps.add({0x350C20, "input: line ground", car_a0, [](TapCall& t) { rules::line_ground(t.car, t.rec.x.input, hooks(t)); }});
    taps.add({0x35A9D0, "input: pedal curve", caller_car, [](TapCall& t) {
                  const u32 at = u32(t.ee.a(0)), slope = u32(t.ee.a(1));
                  const rules::PedalCurve k{t.ee.r32(at), F::raw(t.ee.r32(at + 4)), F::raw(t.ee.r32(at + 8)), F::raw(t.ee.r32(at + 0xC))};
                  F s;
                  t.ret_f("returns", rules::pedal_curve(k, f(t.ee, 12), slope ? &s : nullptr));
                  if (slope) t.mem_f("slope", slope, s);
              }});
    taps.add({0x348800, "input: steer unit", caller_car, [](TapCall& t) { t.ret_f("returns", rules::steer_unit(f(t.ee, 12))); }});
}

} // namespace gt4r::diff
