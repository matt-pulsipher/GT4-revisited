#pragma once

// Who races: a race request's grid (the player's car and the opponents) and the computer drivers' pace
// for its event (docs/specs/ai.md "Who races").

#include "gt4r/formats/db/race_events.h"
#include "gt4r/menu/game.h"
#include "gt4r/sim/ai/drivers.h"

#include <string>
#include <vector>

namespace gt4r::sim::ai {

struct GridEntry {
    std::string car;     // GENERIC_CAR label
    int paint = -1;      // -1: the race chooses
    std::string parts;   // DEFAULT_PARTS label of its parts ("": as sold)
    std::string param;   // DEFAULT_PARAM label of its settings ("": as sold)
    std::string enemy;   // the ENEMY_CARS label it was drawn as ("": the player's or a script's)
    bool player = false;
};

// The pace settings of an event's row.
Pace pace_for(const db::RaceEvent& e);

// The grid of a race request, in grid order: the opponents the scripts chose (request.entries 1 to
// overwrite_enemy_num; entry 0 is the player's), else those drawn from
// the event's pool (request.race) for the event's grid less one (the request's max_entry when the event has none),
// then the player's car last. `seed` seeds the draw (the race's random numbers).
std::vector<GridEntry> grid_for(const db::RaceEvents& events, const menu::RaceRequest& request, u32 seed);

} // namespace gt4r::sim::ai
