#pragma once

// A run kept for showing again (Save Replay): the course, the cars and every step's states, the way
// the race view shows a replay (the cars' states, nothing of the simulation). The program keeps the
// drivers' controls on the memory card and runs the race again (Serialize::ReplayData); this file
// is the rebuild's own: "GT4RRPL1", then a deflated body (docs/formats/race_flow.md "Saved
// replays").

#include "gt4r/base/types.h"
#include "gt4r/race/simulation.h"

#include <string>
#include <vector>

namespace gt4r::race {

struct ReplayFile {
    struct Car {
        std::string label; // the car database's label
        u32 paint = 0;
    };
    std::string course;  // the course file's name
    std::string sequence, race; // what was run ("single_race", the event's label)
    int laps = 0;
    std::vector<Car> cars;
    std::vector<std::vector<CarState>> steps; // a step's states, one per car

    Bytes encode() const;
    // Throws FormatError.
    static ReplayFile decode(ByteSpan file);
    static bool is_replay(ByteSpan d) { return d.size() >= 16 && std::string(reinterpret_cast<const char*>(d.data()), 8) == "GT4RRPL1"; }
};

} // namespace gt4r::race
