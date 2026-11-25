#pragma once

// An online race as the lobby hands it to the race screen (docs/specs/net.md "Lobby"): plain data.
// The lobby decodes the players' cars from their descriptors (net/car_choice.h) and the host's rules
// from its record (net/lobby.h) when the session's Load arrives, the same on every machine.

#include "gt4r/base/types.h"
#include "gt4r/sim/equipment.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::net {

// One car of the race, as every machine builds it from its own disc.
struct RaceCar {
    u8 id = 0;           // gt3r::net::Player::id
    std::string player;  // the player's name
    std::string label;   // the car's label in the database
    u32 paint = 0;       // by index in the car's paints
    std::optional<sim::Equipment> equipment;         // its parts, settings and tires as raced; none: as sold
    std::optional<std::array<u8, 0x20>> condition;   // a garage car's condition block
    std::string wing;    // a fitted wing's label (a WING row); empty: the equipment's
    bool automatic = true, asm_on = true, tcs_on = true, wheel_aid = false; // its owner's
    u8 driver = 0;       // sim::Entry::driver: 0 a pad, 1 and 2 the steering wheels
    u64 digest = 0;      // net::param_digest of the car (equal on every machine)
};

// The race's rules, the host's (menu native getBattleSettingRace, 0x161F28: the 2-player battle's options).
struct RaceRules {
    int laps = 2;           // split_race_laps
    bool tire_wear = false; // split_tire_damage
    int handicap = 0;       // split_handicap
    int boost = 0;          // split_boost_level
    int penalty = 0;        // penalty_type: 0 none, 1 a speed limit, 2 the pit lane
    int heavy = 0;          // LAN_heavy: the network's load
    int ghost_ping_ms = 150; // a pair whose round trip is above this does not collide (0: always collide)
};

struct OnlineRace {
    std::string course;        // the course's label ("tsukuba", "r_tsukuba")
    RaceRules rules;
    std::vector<RaceCar> cars; // in grid order, the pole first
    int ours = 0;              // this machine's index in cars
};

} // namespace gt4r::net
