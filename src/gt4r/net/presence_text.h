#pragma once

// What the Discord status says (app/presence.h): the text of the game's menus and of a race, from what the screens know.

#include <string>

namespace gt4r::net {

// Two short lines (UTF-8) and, online, the lobby's players.
struct PresenceText {
    std::string details;               // first line
    std::string state;                 // second line ("" none)
    int party_size = 0, party_max = 0; // "(2 of 6)" after the second line (0: none)
};

// A race as a status: what kind of run (from the race sequence the menus asked for: "single_race", "license", ...; "" a race set
// up by hand), the course's and the car's names, the lap of the laps, the place of the cars (0: not shown).
struct RacePresence {
    std::string sequence;
    bool online = false, replay = false, demonstration = false;
    std::string course, car;
    int lap = 0, laps = 0, place = 0, cars = 0;
    bool finished = false;
    int players = 0; // online: the lobby's
};
PresenceText race_presence(const RacePresence& r);
// The game's menus as a status: the project running and its page ("arcade", "TopRoot"). Empty details: nothing to say.
PresenceText menu_presence(const std::string& project, const std::string& page);

} // namespace gt4r::net
