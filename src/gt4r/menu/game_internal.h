#pragma once

// What the game natives' files share (game_natives.cpp: MGame, MGarage, the card manager;
// game_car.cpp: MCarGarage; game_records.cpp: the calendar, the records, the presents;
// game_race.cpp: MRaceData and a race's result): the disc's car database and the helpers that
// turn the scripts' values into the program's.

#include "gt4r/formats/db/car_parts.h"
#include "gt4r/formats/db/specdb.h"
#include "gt4r/menu/program_tables.h"
#include "gt4r/menu/prologue_license.h"
#include "gt4r/menu/runtime_internal.h"
#include "gt4r/save/memory_card.h"
#include "gt4r/save/save_library.h"
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
        u32 chip[4] = {};  // its chip words, +0x24 .. +0x33 (the swatch's colours and their flags: ColorChipFace)
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
// ---- game_demo.cpp: MDemonstration.
void install_demonstration(GameNatives& n, Object& game);
// Whether all 16 tests of a license ("l0b", "l0a", "lib", "lia", "l0s") are passed with bronze or better (0x4312B0).
bool license_all_bronze(Database& d, const GameState& s, const std::string& category);
// Whether the player may drive an event himself (the race's menus' IsASpec): it asks for no license (its RACE row's byte
// +0x45), or every test of that license is passed (0x110498).
bool event_aspec_allowed(Database& d, const GameState& s, u8 license);
// The course records (0x433C10: a course's unit, made in order of its code when `make`; -1 none) and a ranking's insert
// (0x433260 by time, 0x433348 by top speed: the place by the value, the real clock's date, the car's code, the pass code;
// false past the tenth). `base` the unit's offset in `block` (the course record's or the machine test's).
int course_record_unit(Database& d, GameState& s, const std::string& course, bool make);
// A ranking entry's pass code for a run (game_race.cpp; kind 0 the time trial, 1 the free run, 2 a license test, 3 / 4 / 5 the machine tests: 400 m, 1000 m, top speed).
std::string ranking_pass_code(Database& d, const RaceResult& r, int kind, u32 time);
// A machine test's unit by its name (0x433C98, the names 0x6A57C0, compared exactly): "0_400" 0, "0_1000" 1, "MAXSPEED" 2,
// "LASVEGAS" 3; -1 any other.
int machine_test_unit(const std::string& name);
bool ranking_insert(Database& d, Bytes& block, size_t base, bool by_speed, u32 value, const std::string& car, const std::string& pass_code);
// Gran Turismo 4 Prologue's main::game.license: install_prologue_license (prologue_license.h).
// A Prologue license run's points (0x19F410) from its RACE row (in Gran Turismo 4's columns), its result's name and time.
s32 prologue_license_points(ByteSpan row, const std::string& result, u32 time_ms);

// ---- game_replays.cpp
// The replays saved in the data folder's replays/, newest first.
std::vector<std::string> saved_replays(const std::string& data_dir);
struct GameNatives;
// The card manager's list in MODE_LOAD_REPLAY (the Replay Theater): those replays as files.
void install_replay_list(GameNatives& n, Object& manager, const std::string& mode);
void install_demo_list(GameNatives& n, Object& manager, bool sub);

// A card file a play list holds (MMemoryCardPlayList: the slide show's photos, the Replay Theater's replays). The program's
// list keeps each as its entry: the card folder's seven letters as a number with the letter after the product in the top byte
// (0x1D1478; save/replay_list.h); search compares entries (0x1D13A0).
struct ListedFile : Object {
    using Object::Object;
    u64 entry = 0;
    bool missing = false; // an entry whose file is not on the card
};
struct PlayListObject : Object {
    PlayListObject() : Object("MMemoryCardPlayList") {}
    std::vector<std::shared_ptr<ListedFile>> files;
    bool changed = false;
    int find(u64 entry) const;
};
// A new list with the natives' members (0x17DDF8: size, append, remove, search, isChanged, clear, order, rotate, getFile).
std::shared_ptr<PlayListObject> make_play_list();

// ---- game_photos.cpp
// The card manager's photo modes (MODE_LOAD_PHOTO_PICTURE, MODE_SAVE_PHOTO_PICTURE, ..._FILM, MODE_PHOTO_PLAYLIST,
// MODE_PHOTO_PRINTLIST) over the photos on the card in `slot`; true when `mode` is one.
bool install_photo_manager(GameNatives& n, Object& manager, const std::string& mode, int slot);
// MMemoryCardPlayList: the photos chosen for the slide show or the printer.
void install_photo_lists(GameNatives& n, script::Module& menu);
// The KB the photos take on the card in slot 1.
int photos_kb(Runtime& rt);

// ---- game_race.cpp
void install_race_classes(GameNatives& n, script::Module& menu);
// A distance driven added to a car's condition (odometer, engine, oil, body) as the race does.
void add_distance(Database& d, CarRecord& rec, float meters);
// A championship's points for a place (0x6A6EF0 of the program), its race count by its label.
constexpr u32 kChampionshipPoints = 0x6A6EF0;
s32 championship_points(Database& d, int place);
int series_race_count(const std::string& label);
// A course's attribute text (MCourseData.GetAttrString, 0x447F08: the UnistrDB string the COURSE row's +0x08 names).
std::string course_attribute(Database& d, const std::string& course);
// A race's pool of opponents (its RACES file): the ENEMY_CARS label and paint (-1: any).
std::vector<std::pair<std::string, int>> race_entries(Database& d, const std::string& race);

// ---- game_controllers.cpp
// MOption's controller settings, and their part of the options' reset (`before`: both ports' text as it was).
void install_controllers(const std::shared_ptr<GameState>& s, Object& option);
void reset_controllers(GameState& s, const std::array<std::string, 2>& before);

// The place of a course's record in the game's course records for the B-spec driver's marks (0x468690, 0x433C10).
std::optional<size_t> bspec_course_unit(Database& d, const GameState& s, std::string label);

struct TransferState; // earlier_saves.cpp

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
    // MGame.getOption: a copy of the options; setOption: a copy made the game's options and applied.
    script::Value copy_option();
    void commit_option(const script::Value& from);
    std::shared_ptr<Object> build_option(GameState& held, std::shared_ptr<GameState> copy);
    script::Value make_game();
    script::Value make_card_manager(script::Args a);
    std::shared_ptr<save::GameSave> card_save, card2_save;
    std::string card2_folder;
    save::GameSave& card();
    // The saved games (no console's card).
    save::SaveLibrary library();
    save::Codes codes();
    // The game written: over the one there, or made anew (and then the one being played).
    save::Result write_game(const std::string& title);
    // What the scripts keep put into the state a save writes (Prologue's license record).
    void collect();
    // The game there read into the state.
    save::Result load_game();
    bool save_failure_told = false, load_failure_told = false, unreadable = false;
    bool checked = false; // Prologue: the boot looked for a game (a state that no boot set up is never saved over one)
    u32 saved_print = 0; // of the game as it was last saved or loaded (no_card.cpp)
    // The card in slot 2 (the data folder's memorycard2/; not connected when there is no such folder).
    save::GameSave& card2();
    // The garage object of a state.
    std::shared_ptr<Object> make_garage(GameState* s);
    // The games loaded beside the one played (loadInstance, for trading cars), kept while the menus run.
    std::vector<std::shared_ptr<GameState>> other_games;
    // The program's generator (0x874D50: MRandom.GetValue, the opponent picker), its state from 0x12345678.
    ProgramRandom random{0x12345678u};
    script::Value photo_print_list = {}; // the photos chosen for the printer (game_photos.cpp)
    std::shared_ptr<TransferState> transfer; // the Status page's transfer from an earlier game (earlier_saves.cpp)
};

// The screen's figures for the option's monitor_size and wide_mode (0x436D60).
void apply_monitor(GameNatives& n);

// The runtime's game natives (none before install_game_natives).
std::shared_ptr<GameNatives> natives_of(Runtime& rt);
// The mods' cars for sale onto the dealers' lists of a script file just run (mod_car_lists.cpp).
void add_mod_cars_to_lists(GameNatives& n, const std::shared_ptr<script::Module>& into);

} // namespace gt4r::menu
