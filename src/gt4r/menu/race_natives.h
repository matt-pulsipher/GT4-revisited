#pragma once

// What the race's menus (the quick-* projects) share with the race: the program's MQuickWork,
// one object the race sequence fills and the scripts read and write (the grid, the quick tune,
// the command chosen). docs/formats/race_flow.md "MQuickWork".

#include "gt4r/base/types.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace gt4r::menu {

class Runtime;
struct GameState;

// A row of the grid table: before a race by grid place, after one by finishing place.
struct QuickRow {
    std::string car; // the car's label
    int paint = 0;
    int power = -1;  // PS (0 is written "--"); -1: the car's catalog value
    int weight = -1; // kg; -1: the car's catalog value
    int tire = 0;   // the scripts' tire index: 0-2 normal, 3-5 sports, 6.. racing
    s64 time_ms = 0x157529FF; // the finish time (the program's "no time" value: none)
};

struct QuickWork {
    static constexpr s32 kNoTime = 0x157529FF;
    // The commands' names, by the number the program keeps (COMMAND_NOTHING is 0).
    static const std::vector<std::string>& command_names();

    std::string command = "COMMAND_NOTHING"; // selectedCommand: the last one the scripts chose
    std::vector<QuickRow> rows;
    std::string race_label, course_label;
    // The integer attributes by their names in the scripts (canReplay, cursorPosition, IsBSpec,
    // QuickTunePowerLevel, ...), with the program's defaults.
    std::map<std::string, s32> ints;
    // The player's car for its catalog line (getCatPs, getCatTq).
    std::string player_car;

    QuickWork();
    s32 get(const std::string& name) const;
    void set(const std::string& name, s32 v) { ints[name] = v; }
    // The command chosen, taken (cleared to COMMAND_NOTHING).
    std::string take_command();
};

// The player's car's line of the time trial's and free run's pages (0x112E78, 0x117718): its
// drivetrain (FR, FF, 4WD, MR, RR), category (GENERIC_CAR +0x11 % 100: normal, racing, tuning,
// concept) and model year (+0x0C); the catalog's power and torque through `player_car`.
void fill_player_car(Runtime& rt, QuickWork& work, const std::string& car_label);

// A license test's next (NEXT_RACE in the license sequence, 0x10FCD0), from its race code with or
// without the region's prefix ("l0b0001", "usl0b0001"; kept as given): tests 1 .. 15 go to the next
// number, test 8 to its license's coffee break test (B l0c0001, A 0002, IB 0003, IA 0004, S 0005), a
// coffee break test to its license's test 9; test 16 and other codes have none ("").
std::string next_license_test(const std::string& code);

// A race request completed as the menus complete the one they hand to a race (the event's course,
// a test's car and grid): menu::Runtime::race_request (runtime_internal.h).
struct RaceRequest;
void complete_race_request(Runtime& rt, RaceRequest& request);
// The race menus' work as a sequence makes it anew (0x127E60, then its own fill): what its pages show before a run.
void start_quick_work(Runtime& rt, const RaceRequest& request);

// The quick work of the game's state (made on first use).
QuickWork& quick_work(GameState& game);

// The race's menus' project for a race sequence ("single_race" -> "quick-arcade"); empty for one
// without.
std::string quick_project(const std::string& sequence);

} // namespace gt4r::menu
