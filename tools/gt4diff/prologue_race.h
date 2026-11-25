#pragma once

// Gran Turismo 4 Prologue's own program set up in the interpreter (tools/gt4diff): its database, a course and a race of
// cars as its own conductor's initialisation leaves them, so that its race step can be run and read
// (docs/formats/prologue.md "The program", docs/specs/physics.md §9). Routines are named by their address in SCPS-15055
// v1.01; nothing of the program is kept.

#include "ee.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/specdb.h"

#include <functional>
#include <string>
#include <vector>

namespace gt4r::diff::prologue {

using gt3r::diff::Arg;
using gt3r::diff::Ee;

// SCPS-15055 v1.01's segments.
constexpr u32 kTextEnd = 0x4FC12C, kBssStart = 0x5AC950, kBssEnd = 0x761F8C, kGp = 0x5B4970;

// The conductor's layout: car i's record (0xC64 bytes) at +0x77A8 + 0xC64 i (0x3B5F98), its parameter block (0xEDC) at
// +0xC8 + 0xEDC i; the car count at +0x107E4; the vtable pointer at +0x10C24.
constexpr u32 kCondSize = 0x11000;
constexpr u32 kCar0 = 0x77A8, kCarSize = 0xC64, kParams0 = 0xC8, kParamsSize = 0xEDC;
constexpr u32 kCondCars = 0x107E4, kCondVtable = 0x10C24;

// What the pad of a player's entry asks for in a step.
struct Pad {
    float steer = 0; // -1 left .. +1 right
    float throttle = 0, brake = 0;
    int shift = 0;
    u8 kind = 3; // the controller (PadFrame::kind)
    const u8* raw = nullptr; // a recorded frame (16 bytes) in place of the rest
};

struct Race {
    u32 cond = 0, info = 0, course = 0, container = 0;
    std::vector<u32> entries;
    int cars = 0;
    u32 car(int i) const { return cond + kCar0 + kCarSize * u32(i); }
    u32 params(int i) const { return cond + kParams0 + kParamsSize * u32(i); }
    Pad pad[6];
};

// Whether the disc's program is Prologue's (the build program_prologue.h recognizes).
bool is_prologue(Disc& disc);
// The program loaded, with the database's tables opened from the disc's files as its loader (0x4070E0) leaves them.
void boot(Ee& ee, Disc& disc);
// A single race on a course with cars as sold, in grid order, set up by the program's own conductor (its constructor and
// initialisation); `human` entries are driven by the pad. False with `err` when the set-up stops. (`r` must stay where it
// is while the race runs: the pad stub reads it.)
// `aids`: the entries' stability and traction control switches (on, as our sessions' entries have them).
// A lesson's race in place of a single race: its conductor's constructor, its RACE row (Prologue's 98 bytes: the rules, by the
// conductor's 0x3B6B20) and each entry's bytes from its equipment to its switches before the set-up.
struct Lesson {
    u32 conductor = 0x3B4750; // DynamicsConductorLicense
    u32 vtable = 0;           // (its own virtuals; 0x539788 DynamicsConductorTraining's: RaceTraining 0x384E50 sets them over the License's)
    std::vector<u8> row;
    std::function<void(Ee&, u32 entry, int car)> entry;
};
bool setup_race(Ee& ee, Disc& disc, db::SpecDb& sdb, const std::string& course, const std::vector<std::string>& labels, int human, Race& r,
                std::string& err, bool aids = true, const Lesson* lesson = nullptr);
// A lesson's demonstration (prologue_demo.cpp) through the program's own step beside ours: the first step that differs.
int run_demo(Disc& disc, const std::string& label, int steps);
// The switches gt4r_diff's runs give the entries: on, GT4R_AIDS=0 off.
bool aids_from_env();
// The game data's layout as the program writes it (prologue_save.cpp): printed.
bool run_save_layout(Disc& disc);
// The pass codes (prologue_passcode.cpp): the program's 0x1A31A0 against ours on `count` sets of fields; true when all equal.
bool run_passcode(Disc& disc, int count);
// Phase 14 (prologue_near.cpp) in a race of `cars` (labels, comma-separated) against the engine's pairs pass.
int run_near(Disc& disc, const std::string& course, const std::string& cars, int steps);
struct NearStats {
    u64 records = 0, clean = 0, with_slip = 0; // car records compared; those equal but for the drivers' zones and contact; those with a slipstream
    std::string err;
};
NearStats near_check(Disc& disc, const std::string& course, const std::string& cars, int steps, bool print);
// The whole step (GT4R_WHOLESTEP): the fields of the record that differ in any of `steps` steps from the program's state;
// false when the run stops early.
bool whole_step_fields(Disc& disc, const std::string& course, const std::string& car, int steps, std::vector<std::string>& fields);
// One race step of the program's own (0x3B7020) for every car.
void step(Ee& ee, Race& r);

// GT4R_CONVERT: the program's spec builder (0x3B3998) and converter (0x3C74B8) against ours on every car of the database
// (prologue_convert.cpp): the cars, those whose block (from the program's spec) is equal word for word, those whose spec
// (ours) is equal byte for byte, and whether the race's start's values the converter reads are equal.
struct ConvertStats {
    int cars = 0, equal_blocks = 0, equal_specs = 0;
    bool race_start_equal = false;
    std::string err;
};
ConvertStats convert_compare(Disc& disc, const std::string& course, const std::string& car, bool print);
int convert_check(Disc& disc, const std::string& course, const std::string& car);

} // namespace gt4r::diff::prologue
