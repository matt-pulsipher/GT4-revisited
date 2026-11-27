#pragma once

// The user's own program set up in the interpreter (tools/gt4diff): the database, a course's road and a
// race of cars as the program's own initialisation leaves them, so that its step can be run and read.
// Routines are named by address (docs/specs/physics.md); nothing of the program is kept.

#include "ee.h"
#include "program_map.h"
#include "gt4r/disc/disc.h"
#include "gt4r/formats/db/specdb.h"

#include <array>
#include <functional>
#include <string>
#include <vector>

namespace gt4r::diff {

using gt3r::diff::Arg;
using gt3r::diff::Ee;

// Gran Turismo 4 SCUS-97328 v1.01 (the running program's: segments(), and pa() for an address).
constexpr u32 kTextEnd = 0x617A14, kBssStart = 0x6D5E00, kBssEnd = 0x8A215C, kGp = 0x6DDDF0;
constexpr Pa kDatabase{0x6235A8};  // the database object: a table's object at +0x134 + 4 x table id
constexpr Pa kSetCar{0x444480};    // (equipment, car)
constexpr Pa kBuildSpec{0x3F3B68}; // (spec, equipment, -, course kind)
constexpr Pa kStatics{0x35F808};   // (conductor or 0): the per-surface tables and the air's globals from the static block
constexpr Pa kConvert{0x361908};   // (parameter block, spec, control source, 0, rule block, condition block)

// The conductor's layout (docs/specs/physics.md "The race step").
constexpr u32 kCondSize = 0x10400;
constexpr u32 kCar0 = 0x6698, kCarSize = 0x10A0;    // car i
constexpr u32 kParams0 = 0x98, kParamsSize = 0x1100; // car i's parameter block
constexpr u32 kF = 0x104;                           // the car's state block
constexpr Co kCondVtable{0x10140}; // (SCPS-17001 v1.05: +0x10128)
constexpr u32 kCondCars = 0x10000 - 0x760, kCondCourse = 0x10000 - 0x3428;
constexpr u32 kCondTimeScale = 0x10000 - 0x3408;

db::SpecDb open_db(Disc& disc);
// The program's pass code (0x44B258) against ours on made-up fields (passcode.cpp).
bool run_passcode(Disc& disc, int count);
// The program loaded, with the database's tables opened from the disc's files as the game's loader leaves them.
void boot(Ee& ee, Disc& disc, db::SpecDb& sdb);

// What the pad of a human entry asks for in a step (the frame the driver input reads).
struct Pad {
    float steer = 0;    // -1 left .. +1 right
    float throttle = 0, brake = 0;
    int shift = 0;      // +1 up, -1 down
    // The frame's other parts (rules_input.h PadFrame); the default is an analog DualShock (kind 3). A digital axis (its
    // flag clear) gives 1 when its value is not 0 (the steering: its sign).
    u8 kind = 3;
    u8 flags = 1 | 2 | 4;  // bit 0 steering, 1 throttle, 2 brake, 3 back axis, 4 third axis analog
    float back = 0, third = 0;
    u8 nitrous = 0;        // +0x07
    std::array<u8, 16> buttons{}; // +0x10 ..
    s16 raw_steer = 0;     // with raw: the frame's steering as it is (+ left)
    bool raw = false;
    const u8* frame = nullptr; // a recorded pad record (0x20 bytes, a demonstration's) given as it is, instead of the above
};
// The frame the pad stub writes for a pad (0x20 bytes).
void pad_frame(const Pad& p, u8* frame);

struct OrigRace {
    u32 cond = 0, info = 0, course = 0, container = 0, runway = 0;
    std::vector<u32> entries;
    int cars = 0;
    u32 car(int i) const { return cond + kCar0 + kCarSize * u32(i); }
    u32 params(int i) const { return cond + kParams0 + kParamsSize * u32(i); }
    Pad pad[6]; // read by the pad stub at each step
    u32 mode = 0;            // the race's mode the conductor is set up with (0 driven, 1 B-spec, 2 / 3 their replays; before_init may set it)
    Disc* disc = nullptr;    // the disc and course the race was set up from
    std::string course_label;
};
// The race setup_race set up last (the one the pad stub reads).
OrigRace* active_race();

// One race step of the program's own (0x34DB30) for every car.
void orig_step(Ee& ee, OrigRace& o);

// A single race on a course ("tsukuba") with cars as sold, in grid order, set up by the program's own
// conductor (its constructor and initialisation). `human`: the entries the pad drives (the others a
// computer driver). Returns false with `err` when the set-up stops.
// `drivers`: the computer drivers run (the course's driver file in the course object; else their start and decision are
// left out).
// `equipment`: each entry's equipment record (0x178 bytes) written over the car as sold (a demonstration's entries).
// `before_init`: run after the conductor's constructor, before its initialisation (to install probes on the set-up).
// `conductor`: another conductor's constructor (0: the single race's, DynamicsConductorSinglePlayer 0x3474C0), and
// `vtable` the class's table written over the constructor's (0: none; a class without a constructor of its own).
bool setup_race(Ee& ee, Disc& disc, db::SpecDb& sdb, const std::string& course, const std::vector<std::string>& labels, int human, OrigRace& o,
                std::string& err, bool drivers = false, const std::vector<std::array<u8, 0x178>>* equipment = nullptr,
                const std::function<void()>& before_init = {}, u32 conductor = 0, u32 vtable = 0);
// (`o` must stay where it is while the race runs: the pad stub reads it.)
// The road's own grid made as long as the field (GT4R_RACE_GRID's places, the rule block's layout kept) in the races set up
// after this: a field longer than the road's list reads past its end in the program (0x3952C0, no bound), which on
// SCPS-17001 v1.05's courses places the cars off the road (the computer drivers' check, ai.cpp); with it the rule block's layout is
// none (+0x21 = -1), as GT4R_RACE_GRID has it.
void set_extend_grid(bool on);

} // namespace gt4r::diff
