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
    if (!request.entries.empty()) {
        for (const std::string& car : request.entries) {
            GridEntry g;
            g.car = car;
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
