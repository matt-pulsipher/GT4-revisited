// The race's random numbers (the generator at 0x621AE8) and the grid drawn from them: the opponents from an event's pool and
// the paints the race chooses, as the program draws them from its start.

#include "test_framework.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/ai/run.h"

#include <cstdio>

using namespace gt4r;

namespace {

Disc* gt4_disc() {
    for (Disc* d : test::reference_discs())
        if (d->info().game == Game::GT4 && sim::Program::load(*d).known()) return d;
    return nullptr;
}

} // namespace

TEST(ai_race_random_steps) {
    // From its start word: 17 x + 0x11 each step, the value x ^ (x turned by 16 bits), as a number in [0, 1).
    sim::ai::RaceRandom r;
    CHECK(r.state() == 0x12345678u);
    const u32 v = r.bits();
    const u32 x = 0x12345678u * 17u + 0x11u;
    CHECK(r.state() == x);
    CHECK(v == (x ^ ((x << 16) | (x >> 16))));
    sim::ai::RaceRandom s;
    for (int i = 0; i < 10000; ++i) {
        const float f = s.next().host();
        CHECK(f >= 0.0f && f < 1.0f);
    }
}

TEST(ai_disc_grid_drawn_as_the_original) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("needs the Gran Turismo 4 disc (GT4R_DISC)");
    db::SpecDb db = sim::ai::open_specdb(*disc);
    const sim::Program program = sim::Program::load(*disc);
    const db::RaceEvents events(db, [disc](const std::string& path) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = disc->volume().find(path);
        return e ? std::optional<Bytes>(disc->volume().read(*e)) : std::nullopt;
    });
    // The Sunday Cup's first race (am_sunday_0001, a pool of 25) as the first race after the program's start, the player's
    // Fiat Panda in its paint 2: the original's grid on its pre-race page (the reference's saved state): Punto HGT Abarth in
    // paint 3, 106 S16 in 4, xB in 2, Polo GTI in 0, 147 Twinspark in 5, the generator's word then 0x9FD59BA6 (30 draws on).
    menu::RaceRequest req;
    req.race = "am_sunday_0001", req.car = "panda_super_ie_90", req.paint = 2;
    sim::ai::RaceRandom r;
    const std::vector<sim::ai::GridEntry> grid = sim::ai::grid_for(events, req, r);
    const std::vector<sim::Equipment> eq = sim::ai::grid_equipment(db, program, events, grid, r);
    const char* cars[] = {"punto_hgt_abarth_00", "_106_s16_03", "xb_15z_x_2wd_03", "polo_gti_5d_01", "_147_20_twinspark_02", "panda_super_ie_90"};
    const int paints[] = {3, 4, 2, 0, 5, 2};
    CHECK(grid.size() == 6 && eq.size() == 6);
    std::printf("  ");
    for (size_t i = 0; i < grid.size() && i < eq.size() && i < 6; ++i) {
        std::printf(" %s %d", grid[i].car.c_str(), eq[i].b(0x18));
        CHECK(grid[i].car == cars[i]);
        CHECK(eq[i].b(0x18) == paints[i]);
    }
    std::printf("; word %08X\n", r.state());
    CHECK(r.state() == 0x9FD59BA6u);
}
