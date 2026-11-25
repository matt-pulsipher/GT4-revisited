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

// The race's random numbers (the generator word at 0x621AE8): 0x12345678 from the program's start (its static set-up
// 0x3BB7E8 with 0x5792D8), each step x' = 17 x + 0x11 (0x579330) giving x' ^ (x' turned by 16 bits), as a number in
// [0, 1) by 0x578598 (its leading one's exponent, the value's bits after the next one as the fraction). Only the race's
// set-up draws from it: the opponents from an event's pool (RaceInformation::vf15, one a place) and the paints the race
// chooses (0x40AA48, one a candidate tried), so its word is 0x12345678 advanced by every such draw since the start.
class RaceRandom {
public:
    static constexpr u32 kStart = 0x12345678;
    explicit RaceRandom(u32 state = kStart) : state_(state) {}
    u32 state() const { return state_; }
    u32 bits();
    gt3r::ee::F next();

private:
    u32 state_;
};

// The program's one generator, kept for the whole run (from RaceRandom::kStart).
RaceRandom& race_random();

// The pace settings of an event's row.
Pace pace_for(const db::RaceEvent& e);

// The grid of a race request, in grid order: the opponents the scripts chose (request.entries 1 to
// overwrite_enemy_num; entry 0 is the player's), else those drawn from
// the event's pool (request.race) for the event's grid less one (the request's max_entry when the event has none),
// then the player's car last. The pool's draw is RaceInformation::vf15's: per place, the generator's number x the pool's
// size (truncated) and on to the next entry not drawn yet (every entry: none left out, 0x448C10 answering 0 in this
// program); once all are drawn, the entry the number falls on.
std::vector<GridEntry> grid_for(const db::RaceEvents& events, const menu::RaceRequest& request, RaceRandom& random);
// The same from a generator word of `seed` (0: the program's start).
std::vector<GridEntry> grid_for(const db::RaceEvents& events, const menu::RaceRequest& request, u32 seed);

} // namespace gt4r::sim::ai
