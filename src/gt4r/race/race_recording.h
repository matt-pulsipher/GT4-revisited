#pragma once

// A race of ours kept as the program keeps one (Serialize::ReplayData: the memory card's saved replay, docs/formats/replay.md
// "A replay of ours"): the race's header from its set-up, the player's records, one a step from the race's first; and the card
// file's bytes. An arcade single race (RaceArcadeSingle), driven by the player or by the B-spec driver.

#include "gt4r/formats/replay/demonstration.h"
#include "gt4r/sim/session.h"

#include <optional>
#include <string>
#include <vector>

namespace gt4r::race {

struct RecordingSetup {
    u64 event = 0, course = 0;           // the database's codes (RACE, COURSE)
    bool bspec = false;                  // the player's car driven by the B-spec driver
    int laps = 0, penalty = 0, tire_damage = 0;
    std::array<u8, 0x40> driver{};       // the B-spec driver's record for the race
    Bytes playlist;                      // the races' music as the options keep it (0x810 bytes), or none
    s64 time_ms = 0, best_ms = -1;       // the player's race time, else his last lap's; his best lap's
};

replay::Demonstration make_recording(const RecordingSetup& setup, const std::vector<sim::Entry>& entries, const std::vector<replay::PadRecord>& records);

// The card file of it (FileGT4ReplayFull's: ReplayData scrambled), and one read.
Bytes recording_file(const replay::Demonstration& d, const sim::Program& program, u32 random);

// B-spec's commands and the pit menu's choice as the record carries them (the bytes from +0x10).
struct RecordMail {
    u8 pit = 0, front = 0, rear = 0, fuel = 0; // +0x10 1 the choice, 2 the refueling stopped
    u8 command = 0, argument = 0;              // +0x14 4 the pace (0 ..), 5 the overtake, 6 the pit
};
replay::PadRecord mail_record(const RecordMail& m);

} // namespace gt4r::race
