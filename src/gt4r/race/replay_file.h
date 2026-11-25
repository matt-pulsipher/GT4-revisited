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
    // The Replay Theater's row (a tagged section "INFO" after the steps, "PICT" the picture): the title the player gave, the
    // run's time (0: none), whether the B-spec driver drove it, the race's mode by the program's name (0x6A0020:
    // "GTMODE_EVENT_RACE") and its number within it (-1 none), and a 64 x 64 picture (RGBA8; empty: none).
    std::string title, mode;
    int submode = -1;
    u32 time_ms = 0;
    bool bspec = false;
    std::vector<u32> picture;
    std::vector<std::pair<u32, Bytes>> other; // the sections this reader does not know, kept

    // The program's own recording of the run (race/race_recording.h: a card's replay file), in a section "RPLD".
    static constexpr u32 kRecording = 0x444C5052;
    const Bytes* recording() const {
        for (const auto& [tag, b] : other)
            if (tag == kRecording) return &b;
        return nullptr;
    }

    Bytes encode() const;
    // Throws FormatError.
    static ReplayFile decode(ByteSpan file);
    static bool is_replay(ByteSpan d) { return d.size() >= 16 && std::string(reinterpret_cast<const char*>(d.data()), 8) == "GT4RRPL1"; }
};

} // namespace gt4r::race
