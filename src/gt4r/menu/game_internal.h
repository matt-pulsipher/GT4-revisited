#pragma once

// What the game natives' files share (game_natives.cpp: MGame, MGarage, the card manager;
// game_car.cpp: MCarGarage; game_records.cpp: the calendar, the records, the presents;
// game_race.cpp: MRaceData and a race's result): the disc's car database and the helpers that
// turn the scripts' values into the program's.

#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/menu/program_tables.h"
#include "gt4r/menu/runtime_internal.h"
#include "gt4r/save/memory_card.h"
#include "gt4r/sim/program.h"
#include "gt4r/sim/showroom.h"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace gt4r::menu {

using s8 = gt3r::s8;

// A database code: a row and its table ({u32 row, u32 table}); -1 none.
constexpr u64 kNoCode = ~u64(0);
constexpr u64 make_code(u32 table, u32 row) { return (u64(table) << 32) | row; }
// The tables codes name.
constexpr u32 kCarTable = 0, kWheelTable = 29, kWingTable = 30, kCourseTable = 0x22, kRaceTable = 0x23, kEnemyTable = 0x26;

// The disc's car database, read when first asked.
struct Database {
    Runtime* rt = nullptr;
    std::unique_ptr<db::SpecDb> db;
    std::string language = "american"; // its name tables' (the menus' language)
    bool tried = false;
    std::vector<db::CarEntry> cars;
    std::map<std::string, size_t> by_label;
    std::vector<db::CourseEntry> courses;
    ProgramTables names; // the makers, tuners, countries and categories by number
    sim::Program program; // the program's tables the car's figures are built with

    db::SpecDb* get(const RuntimeConfig& config);
    const db::CarEntry* car(const std::string& label) const;
    // A row of a table by its label; empty when there is none.
    ByteSpan row(const std::string& table, const std::string& label);
    ByteSpan car_row(const std::string& label) { return row("GENERIC_CAR", label); }

    // Labels and codes. A table's label list by its id: GENERIC_CAR 0, the parts 1 .. 30,
    // COURSE 0x22, RACE 0x23, ENEMY_CARS 0x26 (the lists' own table ids).
    const db::Labels* labels_of(u32 table);
    u64 code(u32 table, const std::string& label);
    std::string label(u64 code);
    // A race's record unit: its row's place in the RACE table's ids (0x4483B8); labels tried
    // as they are, then with "us", then "jp" in front (0x430D20). -1: none.
    int race_unit(const std::string& label);
    std::string race_unit_label(int unit);

    // What the menus show of a car as sold (its default parts' rows).
    struct Paint {
        std::string name;
        u8 rgb[3] = {};
        u8 flags[8] = {}; // the row's bytes +0x24 .. +0x2B
    };
    struct Spec {
        bool engine = false;
        int ps = 0;
        float torque = 0; // kgf.m
        std::string displacement, layout, valves, aspiration, power_rpm, torque_rpm;
        int drivetrain = -1;
        int length = 0, height = 0, weight = 0; // mm, mm, kg; 0: not known
        std::vector<Paint> paints;
    };
    std::map<std::string, Spec> specs;
    const Spec& spec(const std::string& label);

    // The showroom car (sim/showroom.h: ShowRoomCar's figures and evaluation); none without the
    // database or for a program whose tables are not mapped.
    const sim::Showroom* showroom();
    // A car as sold, built so (kept: the database does not change).
    std::optional<sim::ShowroomFigures> stock_showroom(const std::string& label);
    std::unique_ptr<sim::Showroom> showroom_;
    std::map<std::string, std::optional<sim::ShowroomFigures>> stock_figures_;
};

inline script::Value text(const std::string& s) { return script::Value::string(s); }
inline script::Value number(s32 v) { return script::Value::integer(v); }
using Fn = script::NativeFunction;
std::shared_ptr<script::Module> make_class(script::Module& into, const std::string& name);
void static_fn(script::Module& cls, const std::string& name, Fn fn);
// The number a "%.2t" in `format` is given: the value times a hundred, whole.
script::Value fixed_for(const std::string& format, float value);
// `format.format(args...)` as the scripts' strings do.
script::Value format_with(script::Vm& vm, const script::Value& format, std::vector<script::Value> args);

// The program's money text (0x42E6B8): "--" for nothing, else the number with commas.
std::string money_text(s64 v);
// A time in ms as the program writes it (0x42E478): 359,999,999 is the dashes.
std::string time_text(u32 ms);

// The day numbers of the calendar (0x5797A8, 0x579898): the proleptic Gregorian ordinal +
// 1,721,424 (2005-04-02 is 2,453,462).
u32 date_to_day(int y, int m, int d);
void day_to_date(u32 day, int& y, int& m, int& d);

// The program's random numbers (MRandom; 0x579330, 0x578598).
struct ProgramRandom {
    u32 s;
    u32 step() {
        s = s * 17 + 17;
        return s ^ ((s << 16) | (s >> 16));
    }
    float next01();
};

// ---- game_car.cpp
// A car as an MCarGarage object holds it: its own record, or the garage's (the car driven).
struct CarHandle {
    GameState* state = nullptr;
    int garage_slot = -1;  // the garage's place it stands for (-1: none)
    bool refers = false;   // the record is the garage's own (the riding car), not a copy
    CarRecord own;
    std::string label;     // the car's label ("" : vacant)
    // The parts that fit the car (its CARS file): {part code, category}.
    std::vector<std::pair<u64, int>> parts;
    std::string parts_for;
    // The script object's own numbers (0x174 .. 0x188).
    s32 weight = 0, power100 = 0, torque100 = 0, ride_history = 0;
    u32 day = 0;

    CarRecord& record();
    const CarRecord& record() const { return const_cast<CarHandle*>(this)->record(); }
};
// The MCarGarage object.
struct CarObject : Object {
    std::shared_ptr<CarHandle> car;
    CarObject() : Object("MCarGarage") {}
};
CarHandle* car_handle(const script::Value& v);
struct GameNatives;
// The MCarGarage class and its objects.
void install_car_class(GameNatives& n, script::Module& menu);
script::Value make_car_value(GameNatives& n, std::shared_ptr<CarHandle> car);
// A car as sold: its three sheets, paint, owned stock parts, a new engine (the program's
// set-car, 0x440EC8). False when the database has no such car.
bool set_car(Database& d, CarRecord& r, const std::string& label, int paint);
// A garage entry's columns from its record (0x434FD0: the power and the weight).
void entry_from_record(Database& d, GarageEntry& e);
// A car's handle from a garage place: the record when it has one, else built from the entry.
std::shared_ptr<CarHandle> car_from_entry(Database& d, GameState& s, int slot, bool refer);
// The figures of a record's current sheet: power (PS x 100), torque (kgf m x 100), weight.
void car_figures(Database& d, const CarRecord& r, s32& power100, s32& torque100, s32& weight);
// A used car's condition from its odometer (0x440F78).
void make_used_condition(Database& d, CarRecord& r, u32 meters);

// ---- game_records.cpp
void install_records(GameNatives& n, Object& game, script::Module& menu);
// Gran Turismo 4 Prologue's main::game.license (prologue_natives.cpp).
void install_prologue_license(Object& game);
// A Prologue license run's points (0x19F410) from its RACE row (in Gran Turismo 4's columns), its result's name and time.
s32 prologue_license_points(ByteSpan row, const std::string& result, u32 time_ms);

// ---- game_replays.cpp
// The replays saved in the data folder's replays/, newest first.
std::vector<std::string> saved_replays(const std::string& data_dir);
struct GameNatives;
// The card manager's list in MODE_LOAD_REPLAY (the Replay Theater): those replays as files.
void install_replay_list(GameNatives& n, Object& manager);

// ---- game_race.cpp
void install_race_classes(GameNatives& n, script::Module& menu);
// A distance driven added to a car's condition (odometer, engine, oil, body) as the race does.
void add_distance(Database& d, CarRecord& rec, float meters);
// A championship's points for a place (0x6A6EF0 of the program), its race count by its label.
constexpr u32 kChampionshipPoints = 0x6A6EF0;
s32 championship_points(Database& d, int place);
int series_race_count(const std::string& label);
// A race's pool of opponents (its RACES file): the ENEMY_CARS label and paint (-1: any).
std::vector<std::pair<std::string, int>> race_entries(Database& d, const std::string& race);

// The place of a course's record in the game's course records for the B-spec driver's marks (0x468690, 0x433C10).
std::optional<size_t> bspec_course_unit(Database& d, const GameState& s, std::string label);

struct GameNatives {
    Runtime& rt;
    RuntimeConfig config;
    std::shared_ptr<Database> database;
    std::shared_ptr<Object> game, option, sound;

    GameState& state() { return rt.game(); }
    Database& db() {
        database->get(config);
        return *database;
    }
    std::shared_ptr<Object> make_music(const std::string& object);
    script::Value make_sound();
    script::Value make_option();
    script::Value make_game();
    script::Value make_card_manager(script::Args a);
    std::shared_ptr<save::GameSave> card_save, card2_save;
    save::GameSave& card();
    // The card in slot 2 (the data folder's memorycard2/; not connected when there is no such folder).
    save::GameSave& card2();
    // The garage object of a state.
    std::shared_ptr<Object> make_garage(GameState* s);
    // The games loaded beside the one played (loadInstance, for trading cars), kept while the menus run.
    std::vector<std::shared_ptr<GameState>> other_games;
    // The program's generator (0x874D50: MRandom.GetValue, the opponent picker), its state from 0x12345678.
    ProgramRandom random{0x12345678u};
};

// The screen's figures for the option's monitor_size and wide_mode (0x436D60).
void apply_monitor(GameNatives& n);

// The runtime's game natives (none before install_game_natives).
std::shared_ptr<GameNatives> natives_of(Runtime& rt);

} // namespace gt4r::menu
