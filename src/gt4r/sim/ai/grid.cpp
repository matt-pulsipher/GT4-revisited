#include "gt4r/sim/ai/grid.h"

#include <algorithm>

namespace gt4r::sim::ai {

u32 RaceRandom::bits() {
    state_ = state_ * 17u + 0x11u;
    return state_ ^ ((state_ << 16) | (state_ >> 16));
}

gt3r::ee::F RaceRandom::next() {
    const u32 v = bits();
    if (v == 0) return gt3r::ee::F::raw(0);
    // The leading bits equal to bit 31, less one (PLZCW); -1 for a value with bit 31 set.
    s32 lead = -1;
    if (!(v & 0x80000000u)) {
        int n = 0;
        while (!(v & (0x80000000u >> n))) ++n;
        lead = n - 1;
    }
    const s32 e = 0x7D - lead;
    return gt3r::ee::F::raw((u32(e) << 23) | ((v << (u32(-e) & 31)) >> 9));
}

RaceRandom& race_random() {
    static RaceRandom r;
    return r;
}

Pace pace_for(const db::RaceEvent& e) {
    Pace p;
    p.throttle = e.ai_throttle, p.brake = e.ai_brake, p.corner = e.ai_corner;
    // The rubber band (0x365480): percents, metres, tens of metres; the overall share 0 reads as 100.
    p.global = float(e.band_global ? e.band_global : 100) * 0.01f;
    p.slow = float(e.slow_pct) * 0.01f, p.slow_start = float(e.slow_start), p.slow_full = float(e.slow_full) * 10.0f;
    p.fast = float(e.fast_pct) * 0.01f, p.fast_start = float(e.fast_start), p.fast_full = float(e.fast_full) * 10.0f;
    p.band_global = e.band_global, p.band_mode = e.band_mode, p.slow_pct = e.slow_pct, p.slow_start_m = e.slow_start, p.slow_full_10m = e.slow_full;
    p.fast_pct = e.fast_pct, p.fast_start_m = e.fast_start, p.fast_full_10m = e.fast_full;
    // The places' held starts (0x447828: place 0 the row's +0x91, places 1 .. 5 its +0x8C .. +0x90), in seconds.
    if (e.bytes.size() > 0x91) {
        p.start_delay[0] = s16(e.bytes[0x91]);
        for (size_t i = 1; i < 6; ++i) p.start_delay[i] = s16(e.bytes[0x8B + i]);
    }
    return p;
}

std::vector<GridEntry> grid_for(const db::RaceEvents& events, const menu::RaceRequest& request, u32 seed) {
    RaceRandom r(seed ? seed : RaceRandom::kStart);
    return grid_for(events, request, r);
}

std::vector<GridEntry> grid_for(const db::RaceEvents& events, const menu::RaceRequest& request, RaceRandom& random) {
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
            const std::vector<db::Opponent> pool = events.pool(*e);
            const size_t n = pool.size();
            std::vector<bool> used(n, false);
            size_t free = n;
            for (int k = 0; k < grid - 1 && n > 0; ++k) {
                size_t i = size_t(gt3r::ee::to_int(random.next() * gt3r::ee::int_to_f(s32(n)))) % n;
                if (free > 0) {
                    while (used[i]) i = (i + 1) % n;
                    used[i] = true, --free;
                }
                const db::Opponent& o = pool[i];
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
