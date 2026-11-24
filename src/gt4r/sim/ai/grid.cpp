#include "gt4r/sim/ai/grid.h"

#include <algorithm>

namespace gt4r::sim::ai {

Pace pace_for(const db::RaceEvent& e) {
    Pace p;
    p.throttle = e.ai_throttle, p.brake = e.ai_brake, p.corner = e.ai_corner;
    // The rubber band (0x365480): percents, metres, tens of metres; the overall share 0 reads as 100.
    p.global = float(e.band_global ? e.band_global : 100) * 0.01f;
    p.slow = float(e.slow_pct) * 0.01f, p.slow_start = float(e.slow_start), p.slow_full = float(e.slow_full) * 10.0f;
    p.fast = float(e.fast_pct) * 0.01f, p.fast_start = float(e.fast_start), p.fast_full = float(e.fast_full) * 10.0f;
    return p;
}

std::vector<GridEntry> grid_for(const db::RaceEvents& events, const menu::RaceRequest& request, u32 seed) {
    std::vector<GridEntry> out;
    // The scripts' entry table: entry 0 is the player's car (arcade's doGame shifts it for the
    // course like the others), entries 1 to overwrite_enemy_num the opponents they chose.
    if (request.overwrite_enemy_num > 0 && request.entries.size() > 1) {
        for (size_t i = 1; i < request.entries.size() && i <= size_t(request.overwrite_enemy_num); ++i) {
            if (request.entries[i].empty() || request.entries[i] == "nil") continue;
            GridEntry g;
            g.car = request.entries[i];
            out.push_back(g);
        }
    } else if (!request.race.empty()) {
        if (const std::optional<db::RaceEvent> e = events.event(request.race)) {
            const int grid = e->grid > 0 ? e->grid : request.max_entry;
            u32 state = seed ? seed : 1;
            auto random = [&state]() {
                state = state * 1664525u + 1013904223u;
                return float(state >> 8) / float(1u << 24);
            };
            for (const db::Opponent& o : events.draw(*e, std::max(grid - 1, 0), random)) {
                GridEntry g;
                g.car = o.car, g.paint = o.paint, g.parts = o.parts, g.param = o.param, g.enemy = o.enemy;
                out.push_back(g);
            }
        }
    }
    if (!request.car.empty()) {
        GridEntry me;
        me.car = request.car, me.paint = request.paint, me.player = true;
        out.push_back(me);
    }
    return out;
}

} // namespace gt4r::sim::ai
