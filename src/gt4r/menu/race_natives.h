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

// The quick work of the game's state (made on first use).
QuickWork& quick_work(GameState& game);

// The race's menus' project for a race sequence ("single_race" -> "quick-arcade"); empty for one
// without.
std::string quick_project(const std::string& sequence);

} // namespace gt4r::menu
