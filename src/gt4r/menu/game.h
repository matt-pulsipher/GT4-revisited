#pragma once

// The game's state as the menus keep it (`main::game`, the program's MGame, and what hangs
// off it: the options, the garage, the money, the records): plain data, so the app can read
// what the scripts set and a saved game can be put behind it. The scripts' view of it is in
// game_natives.cpp. docs/formats/menu.md ("The game's state").

#include "gt4r/base/types.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace gt4r::menu {

// An option's value: MOption's attributes are numbers and text.
using OptionValue = std::variant<s32, float, std::string>;

// The race the menus set up: MGame's attributes of those names. When the scripts leave the
// menus for a race (`next_sequence` set to a race's name and the project finished), this is
// what the race is started from.
struct RaceRequest {
    std::string sequence; // "single_race", "time_trial", "license", "freerun", ...
    std::string course;   // course_code: "tsukuba", "r_tsukuba" (reverse)
    std::string race;     // race_code: an event of the car database ("" : none)
    std::string game;     // game_code
    std::string car;      // car_code: a car's label ("_117_coupe_68")
    int paint = 0;        // car_color: the car's paint by index
    int laps = 0;
    int difficulty = 0;
    int enemy_level = 0;
    int max_entry = 6;           // cars on the grid
    int overwrite_enemy_num = 0; // opponents chosen by the scripts (entries)
    int assist_type = 0, assist_asm = 0, assist_tcs = 0;
    int manual = 0;    // 1: manual gears
    int vibration = 1; // the pad's
    int tire = 0;      // tire_idx
    // The second player's (split screen).
    std::string car_2p;
    int paint_2p = 0, assist_type_2p = 0, assist_asm_2p = 0, assist_tcs_2p = 0, manual_2p = 0, vibration_2p = 1, tire_2p = 0;
    std::vector<std::string> entries; // the opponents' cars when the scripts chose them
    // The car when it is one of the garage's (-1: a car picked by its label).
    int garage_slot = -1, garage_slot_2p = -1;
};

// A car of the garage.
struct GarageCar {
    std::string car; // its label
    int paint = 0;
    float odometer = 0; // km
    int date = 0;       // the game day it was got
    int ride_order = 0; // larger: ridden later
    bool used = false;
    std::map<std::string, std::string> parts; // by parts type: what is fitted (the car's own when absent)
};

struct GameState {
    // Which project and page the menus go to and came from (MGame's attributes).
    std::string major_menu_project, major_menu_page, major_menu_widget;
    std::string last_sequence = "boot", next_sequence;
    std::string last_menu_project, next_menu_project, last_menu_arg, next_menu_arg, menu_result;
    std::vector<std::string> menu_stack; // menuPush / menuPop / menuRef

    RaceRequest race; // the race being set up

    // MOption's attributes by name. `option_defaults` fills them as a new game has them.
    std::map<std::string, OptionValue> options;
    void option_defaults(const std::string& region);
    s32 option_int(const std::string& name, s32 fallback = 0) const;
    std::string option_text(const std::string& name) const;
    // The options as text lines ("name = value"), and back; unknown names are kept.
    std::string options_text() const;
    void options_from_text(const std::string& text);

    // Gran Turismo mode.
    s64 money = 10000; // a new game's credits
    std::vector<GarageCar> garage;
    int riding_car = -1; // index in the garage; -1: none
    int day = 0;         // the calendar's day
    std::string username, password, last_entry_name;
    std::set<std::string> available_cars, available_courses; // arcade's unlocked ones (labels)
    bool all_available = false;
    std::set<std::string> favorite_cars, favorite_courses;
    std::map<std::string, int> race_results; // by race code: the best result (1 gold ... ; absent: not run)
    s64 total_prize_money = 0;
    int total_prize_cars = 0, score = 0, races = 0;
    bool need_save = false;
    bool during_quick_menu = false;
    int withdrawn = 0;
    int met_type = 0;
    std::string suspended_racecode;

    // The memory card as the menus see it. A console with no card answers "not connected";
    // with an empty card, "no file".
    struct MemoryCard {
        bool connected = true;
        bool formatted = true;
        bool has_game_data = false; // a saved game on it
        int free_kb = 8000;
    } card;
    bool valid_game_data = false; // MMemoryCardManager::setExistValidGameData: a game was loaded or made
};

} // namespace gt4r::menu
