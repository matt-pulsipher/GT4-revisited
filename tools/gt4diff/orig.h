#pragma once

// The user's own program set up in the interpreter (tools/gt4diff): the database, a course's road and a
// race of cars as the program's own initialisation leaves them, so that its step can be run and read.
// Routines are named by address (docs/specs/physics.md); nothing of the program is kept.

#include "ee.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/specdb.h"

#include <array>
#include <string>
#include <vector>

namespace gt4r::diff {

using gt3r::diff::Arg;
using gt3r::diff::Ee;

// Gran Turismo 4 SCUS-97328 v1.01.
constexpr u32 kTextEnd = 0x617A14, kBssStart = 0x6D5E00, kBssEnd = 0x8A215C, kGp = 0x6DDDF0;
constexpr u32 kDatabase = 0x6235A8;  // the database object: a table's object at +0x134 + 4 x table id
constexpr u32 kSetCar = 0x444480;    // (equipment, car)
constexpr u32 kBuildSpec = 0x3F3B68; // (spec, equipment, -, course kind)
constexpr u32 kStatics = 0x35F808;   // (conductor or 0): the per-surface tables and the air's globals from the static block
constexpr u32 kConvert = 0x361908;   // (parameter block, spec, control source, 0, rule block, condition block)

// The conductor's layout (docs/specs/physics.md "The race step").
constexpr u32 kCondSize = 0x10400;
constexpr u32 kCar0 = 0x6698, kCarSize = 0x10A0;    // car i
constexpr u32 kParams0 = 0x98, kParamsSize = 0x1100; // car i's parameter block
constexpr u32 kF = 0x104;                           // the car's state block
constexpr u32 kCondVtable = 0x10140, kCondCars = 0x10000 - 0x760, kCondCourse = 0x10000 - 0x3428;
constexpr u32 kCondTimeScale = 0x10000 - 0x3408;

db::SpecDb open_db(Disc& disc);
// The program loaded, with the database's tables opened from the disc's files as the game's loader leaves them.
void boot(Ee& ee, Disc& disc, db::SpecDb& sdb);

// What the pad of a human entry asks for in a step (the frame the driver input reads).
struct Pad {
    float steer = 0;    // -1 left .. +1 right
    float throttle = 0, brake = 0;
    int shift = 0;      // +1 up, -1 down
    const u8* frame = nullptr; // a recorded pad record (0x20 bytes, a demonstration's) given as it is, instead of the above
};

struct OrigRace {
    u32 cond = 0, info = 0, course = 0, container = 0, runway = 0;
    std::vector<u32> entries;
    int cars = 0;
    u32 car(int i) const { return cond + kCar0 + kCarSize * u32(i); }
    u32 params(int i) const { return cond + kParams0 + kParamsSize * u32(i); }
    Pad pad[6]; // read by the pad stub at each step
};

// One race step of the program's own (0x34DB30) for every car.
void orig_step(Ee& ee, OrigRace& o);

// A single race on a course ("tsukuba") with cars as sold, in grid order, set up by the program's own
// conductor (its constructor and initialisation). `human`: the entries the pad drives (the others a
// computer driver). Returns false with `err` when the set-up stops.
// `drivers`: the computer drivers run (the course's driver file in the course object; else their start and decision are
// left out).
// `equipment`: each entry's equipment record (0x178 bytes) written over the car as sold (a demonstration's entries).
bool setup_race(Ee& ee, Disc& disc, db::SpecDb& sdb, const std::string& course, const std::vector<std::string>& labels, int human, OrigRace& o,
                std::string& err, bool drivers = false, const std::vector<std::array<u8, 0x178>>* equipment = nullptr);
// (`o` must stay where it is while the race runs: the pad stub reads it.)

} // namespace gt4r::diff
