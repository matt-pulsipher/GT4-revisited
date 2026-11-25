#pragma once

// gt4r_diff: the race step from the original's saved states (states.cpp). Two saved states of the original game with a fixed
// pad input held between them (the main memory of each, `eeMemory.bin`, address = offset; its scratchpad and VU0 memory
// beside it when there): state 0's memory copied over the interpreter's, the program's own race step (0x34DB30) run for the
// frames between the two states with the player's pad frame given as the game's pad gave it, and every car's record compared
// with state 1's. Then the same from state 0 with our session (sim::Session), the computer drivers' outputs taken from the
// program's run in lockstep, every placed field compared with the program's run step by step.

#include "orig.h"

#include <array>
#include <string>
#include <vector>

namespace gt4r::diff {

// A pad frame (0x20 bytes, the input phase's) for keys held on the keyboard pad as the original's states have them: "cross",
// "square", "left", "right", "none", joined with '+' ("right+cross"). False for a key it does not know.
bool keys_frame(const std::string& keys, std::array<u8, 0x20>& frame);

struct StatesOptions {
    std::string keys;      // the keys held (keys_frame); empty: the frame the player's controller last read in state 0
    int frames = -1;       // the steps to run; -1: the boot counters' difference (0x0087FA88)
    bool ours = true;      // our session from state 0 too
    bool resync = false;   // ours: every car's record loaded from the program's run before each step (each step alone)
    int window = 2;        // the program's run compared with state 1 at frames - window .. frames + window too
    // ours from its own set-up instead of state 0's records (the grid, the standing start), held this many steps of the
    // countdown before state 0 (-1: from state 0's records); its fields that differ from state 0's are listed first.
    int own_start = -1;
};

struct StatesCar {
    std::string label;
    bool player = false;
    // The program's run against state 1 (after `frames` steps): the placed fields, those equal, and the record's bytes
    // (car and parameter block) that differ; the position's distance (m) and the speed's difference (m/s).
    size_t fields = 0, fields_equal = 0, bytes_off = 0;
    std::string first_field; // the first placed field (in the record's order) that differs, with both values
    double position_error = 0, speed_error = 0;
    // Ours against the program's run: the first step (1 ..) where a placed field differs, the fields differing then, and at
    // the end the position's distance and the speeds (ours, the program's).
    int first_step = -1;
    std::vector<std::string> first_fields;
    double end_position_error = 0, end_speed_ours = 0, end_speed_program = 0;
};

struct StatesResult {
    bool ran = false;
    std::string error, course;
    int frames = 0;
    int best_frames = 0;      // the step count (within the window) where the program's run is nearest to state 1
    size_t best_bytes_off = 0; // the cars' record bytes differing there
    bool frame_matches = true; // the keys' frame equals the one the state's controller last read
    std::vector<StatesCar> cars;
};

StatesResult run_states(Disc& disc, const std::string& state0, const std::string& state1, const StatesOptions& options, bool print);

} // namespace gt4r::diff
