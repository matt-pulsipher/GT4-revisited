// Gran Turismo 4's own small step helpers against the program's routines, word for word, on drawn inputs: the bump wave
// (0x369640; Prologue's 0x375340) and the wheel's hold near standstill (0x3522B0; Prologue's 0x3BAFE0, its wheel record's
// layout).

#include "test_framework.h"

#include "diff.h"
#include "orig.h"
#include "prologue_race.h"
#include "gt4r/sim/rules_body.h"
#include "gt4r/sim/rules_chassis.h"

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
