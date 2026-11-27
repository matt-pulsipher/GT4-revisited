// Gran Turismo 4's own small step helpers against the program's routines, word for word, on drawn inputs: the bump wave
// (0x369640; Prologue's 0x375340) and the wheel's hold near standstill (0x3522B0; Prologue's 0x3BAFE0, its wheel record's
// layout).

#include "test_framework.h"

#include "diff.h"
#include "orig.h"
#include "prologue_race.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_chassis.h"
#include "gt4r/sim/rules_aids.h"
#include "gt4r/sim/rules_tires.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/equipment.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/session.h"
#include "gt4r/formats/db/car_parts.h"

#include <cstdio>
#include <random>

using namespace gt4r;

namespace {

using gt3r::ee::F;

Disc* disc_of(Game game) {
    for (Disc* d : test::reference_discs())
        if (d->info().game == game && (game != Game::GT4 || d->info().serial == "SCUS_973.28")) return d;
    return nullptr;
}

// The wheel record's places the hold reads (Gran Turismo 4's, Prologue's).
struct HoldLayout {
    u32 steer, rim, hub_vx, hub_vy, load;
};

struct Counts {
    size_t calls = 0, equal = 0, nonzero = 0;
};

// Each routine called on the same drawn inputs as ours.
void check(diff::Ee& ee, u32 wave_at, u32 hold_at, const HoldLayout& at, Counts& wave, Counts& hold) {
    std::mt19937 rng(4242);
    const auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
    for (int k = 0; k < 4000; ++k) {
        const int mode = k % 3;
        const F x = F(uni(-3000, 3000)), z = F(uni(-3000, 3000)), f = F(uni(0, 0.6f));
        const u32 theirs = ee.call(wave_at, {diff::Arg::integer(mode), diff::Arg::fbits(x.bits), diff::Arg::fbits(z.bits), diff::Arg::fbits(f.bits)}).f0;
        const u32 ours = sim::rules::bump_wave(mode, x, z, f).bits;
        ++wave.calls, wave.equal += theirs == ours, wave.nonzero += ours != 0;
    }
    const u32 w = ee.alloc(0x100);
    // (the edges themselves among the draws: 5, 2 and 4 km/h, and a unit either side)
    const u32 edges[] = {0x3FB1C71B, 0x3FB1C71A, 0x3FB1C71C, 0xBFB1C71B, 0xBFB1C71C, 0x3F0E38E3, 0x3F0E38E4, 0xBF0E38E3, 0xBF0E38E4, 0x3F8E38E3, 0x3F8E38E4, 0xBF8E38E3};
    for (int k = 0; k < 20000; ++k) {
        gt3r::sim::exact::Wheel wh{};
        wh.rim_speed = F(uni(-1.8f, 1.8f));
        wh.hub_vx = wh.rim_speed + F(uni(-0.7f, 0.7f));
        wh.hub_vy = F(uni(-1.4f, 1.4f));
        wh.steer = F(uni(-0.7f, 0.7f));
        wh.peak_force = k % 17 == 0 ? F() : F(uni(0, 6000));
        if (k % 5 == 0) wh.rim_speed = F::raw(edges[size_t(k / 5) % std::size(edges)]);
        if (k % 7 == 0) wh.hub_vx = wh.rim_speed + F::raw(edges[size_t(k / 7) % std::size(edges)]);
        for (u32 o = 0; o < 0x100; o += 4) ee.w32(w + o, 0);
        ee.w32(w + at.steer, wh.steer.bits), ee.w32(w + at.rim, wh.rim_speed.bits), ee.w32(w + at.hub_vx, wh.hub_vx.bits);
        ee.w32(w + at.hub_vy, wh.hub_vy.bits), ee.w32(w + at.load, wh.peak_force.bits);
        const u32 theirs = ee.call(hold_at, {diff::Arg::ptr(w)}).f0;
        const u32 ours = sim::rules::low_speed_hold(wh).bits;
        ++hold.calls, hold.equal += theirs == ours, hold.nonzero += ours != 0;
    }
}

} // namespace

TEST(disc_bump_wave_and_low_speed_hold_equal_the_programs) {
    bool any = false;
    if (Disc* disc = disc_of(Game::GT4)) {
        any = true;
        db::SpecDb sdb = diff::open_db(*disc);
        diff::Ee ee;
        diff::boot(ee, *disc, sdb);
        Counts wave, hold;
        check(ee, 0x369640, 0x3522B0, {0x00, 0x20, 0x28, 0x2C, 0x4C}, wave, hold);
        std::printf("  Gran Turismo 4: bump wave %zu of %zu equal (%zu not 0), hold %zu of %zu equal (%zu not 0)\n", wave.equal, wave.calls,
                    wave.nonzero, hold.equal, hold.calls, hold.nonzero);
        CHECK(wave.equal == wave.calls && hold.equal == hold.calls && hold.nonzero > 1000 && hold.nonzero < hold.calls);
    }
    if (Disc* disc = disc_of(Game::Prologue)) {
        any = true;
        diff::Ee ee;
        diff::prologue::boot(ee, *disc);
        Counts wave, hold;
        check(ee, 0x375340, 0x3BAFE0, {0x0C, 0x20, 0x4C, 0x50, 0x54}, wave, hold);
        std::printf("  Prologue: bump wave %zu of %zu equal (%zu not 0), hold %zu of %zu equal (%zu not 0)\n", wave.equal, wave.calls,
                    wave.nonzero, hold.equal, hold.calls, hold.nonzero);
        CHECK(wave.equal == wave.calls && hold.equal == hold.calls && hold.nonzero > 1000 && hold.nonzero < hold.calls);
    }
    if (!any) SKIP("needs the Gran Turismo 4 or the Prologue disc (GT4R_DISC)");
}

// Prologue's made gearboxes (sim::computed_gears: its 0x406190 over the generator 0x3E0BE0) against the program's own 0x406190
// on the equipment its 0x407A08 makes for the car, word for word at every slider, on every car whose gearbox is made.
TEST(disc_prologue_gear_ratios_equal_the_programs) {
    Disc* disc = disc_of(Game::Prologue);
    if (!disc) SKIP("needs the Prologue disc (GT4R_DISC)");
    db::SpecDb sdb = diff::open_db(*disc);
    const sim::Program program = sim::Program::load(*disc);
    diff::Ee ee;
    diff::prologue::boot(ee, *disc);
    std::printf("  the double at 0x572CB8: %016llX\n", (unsigned long long)ee.r64(0x572CB8));
    CHECK(ee.r64(0x572CB8) == 0x3F50624DD2F1A9FBull);
    const u32 eq_at = ee.alloc(0x400, 64);
    size_t cars = 0, checked = 0, equal = 0;
    for (const db::CarEntry& c : sdb.cars()) {
        sim::Equipment eq;
        if (!sim::stock_equipment(sdb, c.label, eq)) continue;
        sim::Equipment probe = eq;
        if (!sim::computed_gears(sdb, program, probe)) continue;
        sim::Spec s;
        sim::build_spec(sdb, program, eq, 0, s);
        const int n = s.gears();
        ++cars;
        for (int slider = 10; slider <= 50; slider += 2) {
            eq.set_b(sim::Equipment::GearSlider, u8(slider));
            sim::Equipment ours = eq;
            sim::computed_gears(sdb, program, ours);
            for (u32 o = 0; o < 0x400; o += 4) ee.w32(eq_at + o, 0);
            ee.call(0x407A08, {diff::Arg::ptr(eq_at), diff::Arg::integer(s64(db::part_id(0, c.id)))}, 50'000'000);
            ee.w8(eq_at + 0x10A, u8(slider));
            ee.call(0x406190, {diff::Arg::ptr(eq_at)}, 50'000'000);
            bool same = true;
            for (int i = 0; i <= n; ++i) same &= ee.r16(eq_at + 0xF8 + 2 * u32(i)) == ours.h(sim::Equipment::GearRatios + 2 * u32(i));
            if (!same && checked - equal < 4) {
                std::printf("  %s slider %d:", c.label.c_str(), slider);
                for (int i = 0; i <= n; ++i) std::printf(" %d/%d", s16(ee.r16(eq_at + 0xF8 + 2 * u32(i))), s16(ours.h(sim::Equipment::GearRatios + 2 * u32(i))));
                std::printf("\n");
            }
            ++checked, equal += same;
        }
    }
    std::printf("  %zu cars with a made gearbox, %zu car and slider pairs checked, %zu equal\n", cars, checked, equal);
    CHECK(cars > 10 && equal == checked);
}

// Prologue's conductors' pit rule (sim::prologue::PitRule, what the race sets with Session::set_pit_rule): each conductor's
// virtuals 43 (whether a car may go to the pits) and 39 (the mode) called in the program: the single race's and the machine
// test's 1 / 0, a license's and a training's 0 / 3, the free run's 0 / 0.
TEST(disc_prologue_conductors_pit_rule) {
    Disc* disc = disc_of(Game::Prologue);
    if (!disc) SKIP("needs the Prologue disc (GT4R_DISC)");
    diff::Ee ee;
    diff::prologue::boot(ee, *disc);
    const u32 self = ee.alloc(0x20000, 64);
    struct Row {
        const char* name;
        u32 vtable;
        u32 pits, mode;
    };
    const Row rows[] = {{"single race", 0x539608, 1, 0}, {"machine test", 0x539488, 1, 0}, {"license", 0x539308, 0, 3},
                        {"training", 0x539788, 0, 3},    {"free run", 0x53A0D8, 0, 0}};
    for (const Row& r : rows) {
        const u32 pits = ee.call(ee.r32(r.vtable + 0x15C), {diff::Arg::ptr(self)}).v0_s32();
        const u32 mode = ee.call(ee.r32(r.vtable + 0x13C), {diff::Arg::ptr(self)}).v0_s32();
        std::printf("  %s: pits %u, mode %u\n", r.name, pits, mode);
        CHECK(pits == r.pits && mode == r.mode);
    }
}

// A Prologue race on Grand Canyon is on a dirt course (its spec's dirt rule, core_spec: the course's kind 2, as the program's
// builder is handed it).
TEST(disc_prologue_grand_canyon_is_dirt) {
    Disc* disc = disc_of(Game::Prologue);
    if (!disc) SKIP("needs the Prologue disc (GT4R_DISC)");
    const std::string cars[] = {"civic_r_98"};
    const auto s = sim::Session::open(*disc, "grandcanyon", cars);
    std::printf("  grandcanyon: loose %d, course kind %d\n", int(s->loose()), int(s->course_kind()));
    CHECK(s->loose() && s->course_kind() == 2);
}

// The slip angle's fold (0x352218; Prologue's 0x3BAF48), a wheel's slip direction and fade (0x3575E0; 0x3BF4E8) and TCS's
// lateral factor (0x3F9C80; 0x3E18F0): Gran Turismo 4's own against both programs' routines on drawn inputs, word for word.
TEST(disc_slip_angle_and_tcs_factor_equal_the_programs) {
    struct At {
        u32 fold, slip, tcs;
    };
    const auto check = [](diff::Ee& ee, const At& at, const char* name) {
        std::mt19937 rng(777);
        const auto uni = [&](float a, float b) { return std::uniform_real_distribution<float>(a, b)(rng); };
        const u32 out = ee.alloc(16);
        size_t calls = 0, equal = 0;
        for (int k = 0; k < 6000; ++k) {
            const F b = k % 50 == 0 ? F::raw(k % 100 ? 0xBFC90FDA : 0x3FC90FDA) : F(uni(-3.3f, 3.3f));
            const u32 theirs = ee.call(at.fold, {diff::Arg::fbits(b.bits), diff::Arg::ptr(out)}).f0, their_sign = ee.r32(out);
            F sign;
            const F ours = sim::rules::slip_angle_fold(b, sign);
            ++calls, equal += theirs == ours.bits && their_sign == sign.bits;
            const F vx = F(uni(-4, 4)), vy = F(uni(-4, 4));
            ee.w32(out, 0x12345678);
            const u32 dir = ee.call(at.slip, {diff::Arg::fbits(vx.bits), diff::Arg::fbits(vy.bits), diff::Arg::ptr(out)}).f0, fade = ee.r32(out);
            F our_fade;
            const F our_dir = sim::rules::slip_angle(vx, vy, our_fade);
            ++calls, equal += dir == our_dir.bits && fade == our_fade.bits;
            const F x = F(uni(-30, 30)), a = F(uni(0, 0.5f)), c = F(uni(0, 10));
            const u32 lat = ee.call(at.tcs, {diff::Arg::fbits(x.bits), diff::Arg::fbits(a.bits), diff::Arg::fbits(c.bits)}).f0;
            ++calls, equal += lat == sim::rules::tcs_lateral(x, a, c).bits;
        }
        std::printf("  %s: %zu of %zu calls equal\n", name, equal, calls);
        CHECK(equal == calls);
    };
    bool any = false;
    if (Disc* disc = disc_of(Game::GT4)) {
        any = true;
        db::SpecDb sdb = diff::open_db(*disc);
        diff::Ee ee;
        diff::boot(ee, *disc, sdb);
        check(ee, {0x352218, 0x3575E0, 0x3F9C80}, "Gran Turismo 4");
    }
    if (Disc* disc = disc_of(Game::Prologue)) {
        any = true;
        diff::Ee ee;
        diff::prologue::boot(ee, *disc);
        check(ee, {0x3BAF48, 0x3BF4E8, 0x3E18F0}, "Prologue");
    }
    if (!any) SKIP("needs the Gran Turismo 4 or the Prologue disc (GT4R_DISC)");
}
