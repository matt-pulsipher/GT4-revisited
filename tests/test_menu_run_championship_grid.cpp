// A championship's grid from its second race on (menu/game.h championship_grid_places, 0x3EE9F8): the opponents by
// weight over power, ties in entry order to the next free place, the player last.

#include "menu_run_flow.h"

#include "gt4r/formats/db/race_events.h"
#include "gt4r/menu/game.h"
#include "gt4r/menu/game_internal.h"
#include "gt4r/sim/ai/entrant.h"
#include "gt4r/sim/ai/grid.h"
#include "gt4r/sim/showroom.h"

#include <cstdio>

TEST(menu_championship_grid_by_weight_over_power) {
    // kg / PS: the player's 10 (not ranked), then the opponents' 3, 2, 5, 2, 2: the three 2s tie at the front in
    // entry order, the 3 next, the 5 last of them, the player behind.
    const std::vector<s32> kg = {1000, 1200, 1000, 1500, 1100, 900};
    const std::vector<s32> ps = {100, 400, 500, 300, 550, 450};
    CHECK((menu::championship_grid_places(kg, ps) == std::vector<int>{5, 3, 0, 4, 1, 2}));
    // The quotient is an integer's: 1,099 kg over 550 PS is 1, ahead of the 2s.
    const std::vector<s32> kg2 = {1000, 1200, 1000, 1500, 1099, 900};
    CHECK((menu::championship_grid_places(kg2, ps) == std::vector<int>{5, 3, 1, 4, 0, 2}));
}

// The reference's Tuning Car Grand Prix (21-gtmode-deep): race 1's grid RUF RGT, HPA R32, RUF BTR, RUF CTR, the FALKEN RX-7,
// the player's R34 last; race 2's HPA R32, BTR, CTR, RGT, RX-7, the R34. The opponents with the event's parts (its pool's
// DEFAULT_PARTS) as showroom cars: 1,485 kg / 529 PS (2), 1,180 / 401 (2), 1,150 / 469 (2), 1,330 / 399 (3), 1,230 / 409 (3).
TEST(disc_menu_championship_grid_race_two) {
    Disc* disc = gt4_disc();
    if (!disc) SKIP("set GT4R_DISC to a Gran Turismo 4 disc");
    Flow f(*disc, "US");
    f.rt->boot();
    const std::shared_ptr<menu::GameNatives> n = menu::natives_of(*f.rt);
    menu::Database& d = n->db();
    db::SpecDb* sdb = d.db.get();
    CHECK(sdb && d.showroom());
    if (!sdb || !d.showroom()) return;
    const db::RaceEvents events(*sdb, [disc](const std::string& p) -> std::optional<Bytes> {
        const gt3r::Volume::Entry* e = disc->volume().find(p);
        if (!e) return std::nullopt;
        return disc->volume().read(*e);
    });
    const sim::Program program = sim::Program::load(*disc);
    const std::optional<db::RaceEvent> e = events.event("pr_tuning_0501");
    CHECK(e.has_value());
    if (!e) return;
    // Race 1's grid as the entrants (entry 0 the player's).
    const char* const race1[] = {"r34_gtr_v2_nur_02", "r_gt_00", "hpa_golf_r32_04", "btr_86", "ctr_87", "murao_rx7_03"};
    std::vector<sim::ai::GridEntry> grid;
    for (const char* car : race1) {
        sim::ai::GridEntry g;
        g.car = car;
        for (const db::Opponent& o : events.pool(*e))
            if (o.car == car) g.parts = o.parts, g.param = o.param, g.enemy = o.enemy;
        grid.push_back(g);
    }
    const std::vector<sim::Equipment> eq = sim::ai::grid_equipment(*sdb, program, events, grid, 1);
    std::vector<s32> kg, ps;
    for (size_t i = 0; i < grid.size(); ++i) {
        const std::optional<sim::ShowroomFigures> fig = i < eq.size() ? d.showroom()->figures(eq[i]) : std::nullopt;
        kg.push_back(fig ? s32(fig->weight) : 0), ps.push_back(fig ? gt3r::ee::to_int(fig->power) : 0);
        std::printf("  %-20s %d kg %d PS\n", race1[i], kg.back(), ps.back());
    }
    CHECK((menu::championship_grid_places(kg, ps) == std::vector<int>{5, 3, 0, 1, 2, 4}));
}
