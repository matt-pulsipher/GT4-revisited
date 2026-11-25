#pragma once

// A saved replay's or photo's particulars (its folder's 256-byte `thumbnail` file, docs/formats/save.md "The game's other
// saves"): what the Replay Theater's rows show. Scrambled as the other files (save/codec.h, FileKind::Particulars).

#include "gt4r/base/types.h"

#include <optional>
#include <string>

namespace gt4r::save {

struct ReplayParticulars {
    u64 car = 0, course = 0;                   // the database's codes
    std::string title, course_label, car_name; // 63 letters each (the car's name is made again from its code: 0x1CF1E8)
    u32 time_ms = 0;
    int mode = 0;     // into kReplayModes
    int submode = -1; // a test's or a mission's number (-1 none)
    bool spectator = false; // the B-spec driver's run
    bool license = false;   // a license test's (the list's other picture)
};

// The races' modes by the program's names (0x6A0020), as the scripts get them (`racemode`).
inline constexpr const char* kReplayModes[] = {
    "NOTHING",         "ARCADE_SINGLE_RACE",          "ARCADE_TIME_TRIAL",      "GTMODE_LICENSE",      "GTMODE_LICENSE_B",
    "GTMODE_LICENSE_A", "GTMODE_LICENSE_IB",          "GTMODE_LICENSE_IA",      "GTMODE_LICENSE_S",    "GTMODE_LICENSE_COFFEE_BREAK",
    "GTMODE_LICENSE_MISSION", "GTMODE_FREE_RUN",      "GTMODE_FREE_PRACTICE",   "GTMODE_EVENT_RACE",   "GTMODE_CHAMPIONSHIP",
    "GTMODE_MACHINE_TEST", "GTMODE_MACHINE_TEST_0_400", "GTMODE_MACHINE_TEST_0_1000", "GTMODE_MACHINE_TEST_MAX_SPEED",
};

std::optional<ReplayParticulars> decode_particulars(ByteSpan file);
// The file with the title changed (its other bytes kept); none when it is no such file.
std::optional<Bytes> rename_particulars(ByteSpan file, const std::string& title);

} // namespace gt4r::save
