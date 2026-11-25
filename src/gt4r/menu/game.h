#pragma once

// The game's state as the menus keep it (`main::game`, the program's MGame, and what hangs
// off it: the options, the garage, the money, the records): plain data, so the app can read
// what the scripts set and a saved game can be put behind it. The scripts' view of it is in
// game_natives.cpp. docs/formats/menu.md ("The game's state").

#include "gt4r/base/types.h"
#include "gt4r/sim/equipment.h"

#include <algorithm>
#include <array>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace gt4r::menu {

struct QuickWork; // race_natives.h

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
    std::string difficulty = "NORMAL"; // "EASY", "NORMAL", "HARD" (the scripts' names)
    int enemy_level = 0;               // -10 to 10: the opponents' pace when the car has no race of its own
    int max_entry = 6;           // cars on the grid
    int overwrite_enemy_num = 0; // opponents chosen by the scripts (entries)
    int assist_type = 0, assist_asm = 0, assist_tcs = 0;
    int manual = 0;    // 1: manual gears
    int vibration = 1; // the pad's
    int tire = 0;      // tire_idx
    // The second player's (split screen).
    std::string car_2p;
    int paint_2p = 0, assist_type_2p = 0, assist_asm_2p = 0, assist_tcs_2p = 0, manual_2p = 0, vibration_2p = 1, tire_2p = 0;
    // The entry table the scripts filled (setEntryCarCode): by the entry's number, 0 the player's
    // car (also car / paint), the opponents they chose from 1 to overwrite_enemy_num ("" none).
    std::vector<std::string> entries;
    std::vector<int> entry_paints;
    // The "replay" sequence (the Replay Theater's Play): the saved replay to show, a file of the
    // data folder's replays/ (race/replay_file.h).
    std::string replay_file;
    // The car when it is one of the garage's (-1: a car picked by its label).
    int garage_slot = -1, garage_slot_2p = -1;
    // The B-spec driver (menu/game_bspec.h): its book as the game has it and the course's marks (the course
    // record's +0x228 bytes; none when the course has no record). The race makes the driver's record of 0x40
    // bytes for the car's class with bspec::race_record (0x468710).
    std::array<u16, 3 * 64> bspec_book{};
    std::array<u8, 8> bspec_course_marks{};
    bool bspec_course_known = false;
};

// No time: the program's 359,999,999 ms (shown "--'--.---").
constexpr u32 kNoTime = 359'999'999;

// What a race gives back to the menus (the race side fills it): the program's result routines
// (0x114498 an event, 0x119CB8 / 0x119F28 a championship's race and its last, 0x1105A8 a
// license test, 0x11C328 a mission, 0x115A90 on a course's run) write it into the game's
// records, the diary, the counters and the race's menus (MQuickWork: the prize, the rows in
// finishing order, the player's place; a championship's points, grand prize and rank).
struct RaceResult {
    enum class Kind { Event, License, Mission, Course, Championship };
    Kind kind = Kind::Event;
    std::string race;      // the RACE label as the request had it ("am_sunday_0001", "l0b0001"); a championship's race: the race run ("..._0501")
    std::string series;    // a championship: its own label ("..._0500")
    std::string course;    // the course's label (a course's run)
    int course_kind = 1;   // a course's run: 1 free run, 2 event, 3 practice, 4 photo, 6 machine test, 7 time trial
    bool finished = true;
    bool watched = false;  // the player watched (Spectator, B-spec): no record of the player's own driving
    int place = 0;         // 0: first
    std::string result;    // a test's: "gold", "silver", "bronze", "failed", ... (the result names)
    u32 time_ms = 0;       // the whole race's time
    std::string pass_code; // a ranking entry's (up to 32 characters)
    std::string name;      // a ranking entry's name (Prologue's license results: the three letters entered; empty: none)
    float distance_m = 0;  // what the car driven covered
    int score = -1;        // the race's A-spec points (-1: none)
    int garage_slot = -1;  // the car driven's place in the garage (-1: not a garage car)
    // Every car in finishing order (the player's place in it is `place` when it finished).
    struct Finisher {
        int entry = 0;     // 0: the player's; else the entry's index on the grid (the same car through a championship)
        std::string car;   // its label
        int paint = 0;
        bool finished = true;
        u32 time_ms = kNoTime;
    };
    std::vector<Finisher> order;
    // The B-spec driver's record after the race (the car's +0x8C), when the race kept one: written back into
    // the book for the car's class (bspec::write_back, 0x468860), the change doubled (0x468A60: race_finished works it
    // out from the result, a race of two cars or more won; `bspec_doubled` forces it), the course's marks too with
    // `bspec_course` (the race's byte cond + 0xCBE7, 0x468A40: the request's bspec_course_known, from the course's
    // attribute text).
    std::optional<std::array<u8, 0x40>> bspec_record;
    int bspec_class = 0;
    bool bspec_doubled = false, bspec_course = false;
};

// A championship's grid from its second race on (0x3EE9F8, the championship's race information when its
// entrants are kept, and NEXT_RACE's 0x118EF0): each entry's showroom car (0x364868 with a fresh condition)
// gives its weight (kg, 0x364FA8) over its power (PS truncated, 0x364E90), an integer quotient; each
// opponent (entries 1 ..) is placed by how many other opponents have a smaller quotient (a tie counts
// for neither), taking the next free place up when that one is taken (in entry order); the player
// (entry 0) is last. `kg` and `ps` by entry; the places, 0 the front, by entry.
std::vector<int> championship_grid_places(const std::vector<s32>& kg, const std::vector<s32>& ps);
class Runtime;
// The same for a race's entries (entry 0 the player's): each car's label and its race equipment (none: as sold), built
// as the showroom car; empty when the runtime has no showroom.
std::vector<int> championship_grid_places(Runtime& rt, const std::vector<std::pair<std::string, std::optional<sim::Equipment>>>& entries);

// A car's record as the garage keeps it: the program's CarGarage (0x4B8 bytes, saved as they
// are; docs/formats/save.md "The car record"): three settings sheets, each a car equipment record
// (sim/equipment.h), the wheel chosen for each tire category, the sheet in use, the parts owned
// and the car's condition.
struct CarRecord {
    static constexpr size_t kSize = 0x4B8, kSheet = 0x178;
    std::array<u8, kSize> bytes{};

    CarRecord(); // no car: every part none (the constructor's record)
    u8* sheet(int i) { return bytes.data() + kSheet * size_t(std::clamp(i, 0, 2)); }
    const u8* sheet(int i) const { return bytes.data() + kSheet * size_t(std::clamp(i, 0, 2)); }
    u8* active() { return sheet(active_sheet()); }
    const u8* active() const { return sheet(active_sheet()); }
    int active_sheet() const { return std::clamp(int(u32at(0x488)), 0, 2); }
    void set_active_sheet(int i) { put32(0x488, u32(i)); }
    s64 wheel(int category) const { return s64(u64at(0x468 + 8 * size_t(category & 3))); }
    void set_wheel(int category, s64 id) { put64(0x468 + 8 * size_t(category & 3), u64(id)); }
    // The parts owned: 96 bits from 0x48C (bit n: byte n / 8, bit n % 8).
    bool owned(int bit) const { return bit >= 0 && bit < 96 && (bytes[0x48C + size_t(bit) / 8] >> (bit % 8)) & 1; }
    void set_owned(int bit, bool on) {
        if (bit < 0 || bit >= 96) return;
        u8& b = bytes[0x48C + size_t(bit) / 8];
        b = on ? u8(b | 1 << (bit % 8)) : u8(b & ~(1 << (bit % 8)));
    }
    // The condition: odometer (meters), the engine's and the oil's distances (meters; a new
    // engine starts at -300,000), the dirt (0 .. 0.8), the body's distance; no wear at all.
    u32 odometer() const { return u32at(0x498); }
    void set_odometer(u32 m) { put32(0x498, m); }
    float condition(size_t field) const; // 1 engine, 2 oil, 3 dirt, 4 body
    void set_condition(size_t field, float v);
    bool no_wear() const { return bytes[0x4AC] != 0; }
    void set_no_wear(bool on) { bytes[0x4AC] = on; }

    u32 u32at(size_t o) const { return u32(bytes[o]) | u32(bytes[o + 1]) << 8 | u32(bytes[o + 2]) << 16 | u32(bytes[o + 3]) << 24; }
    u64 u64at(size_t o) const { return u64(u32at(o)) | u64(u32at(o + 4)) << 32; }
    void put32(size_t o, u32 v) { bytes[o] = u8(v), bytes[o + 1] = u8(v >> 8), bytes[o + 2] = u8(v >> 16), bytes[o + 3] = u8(v >> 24); }
    void put64(size_t o, u64 v) { put32(o, u32(v)), put32(o + 4, u32(v >> 32)); }
};

// A place of the garage's list (the program's 0x20-byte entry; save.md "The garage list"). The
// list is not compacted: a car keeps its place, a sold car leaves a free one.
struct GarageEntry {
    bool used = false;
    std::string car;     // its label
    int paint = 0;       // 7 bits
    u32 power = 0;       // PS x 100, 18 bits: the list's column, from the record
    u32 weight = 0;      // kg, 13 bits
    u32 day = 0;         // the day number it was got on
    u32 odometer = 0;    // meters, as it came (a used car's)
    int ride_rank = 0x3FF; // 0: the car driven now; each change moves the others up; 0x3FF never driven
    u64 kept_flags = 0;  // the bit field's bits not named here, as loaded
    u32 kept_day = 0;  // the day word's top byte, as loaded
    u64 kept_rank = 0; // the rank word's other bits, as loaded
    std::optional<CarRecord> record; // the full record: a car that has been driven
};

// A day of the calendar's diary (16 bytes; save.md "The calendar"): what was done that day.
struct DayEvent {
    u8 type = 0; // 0 nothing, 1 GET_CAR, 2 SELL_CAR, 3 RUN_RACE, 4 RUN_LICENSE, 5 RUN_MISSION, 6 BUY_WHEEL, 7 BUY_WING, 8 RUN_COURSE
    u8 b1 = 0, b2 = 0, b3 = 0;
    u32 word = 0; // a time in ms
    u64 code = 0; // a database code
};

// A ranking (a license test's, a course's): 10 entries, best first, kept as the program's
// bytes; and a course's unit has its course's code.
constexpr size_t kLicenseUnit = 0x1F8, kLicenseUnits = 125, kCourseUnit = 0x238, kCourseUnits = 128, kMachineUnits = 4;

struct GameState {
    // Which project and page the menus go to and came from (MGame's attributes).
    std::string major_menu_project, major_menu_page, major_menu_widget;
    std::string last_sequence = "boot", next_sequence;
    std::string last_menu_project, next_menu_project, last_menu_arg, next_menu_arg, menu_result;
    std::vector<std::string> menu_stack; // menuPush / menuPop / menuRef

    RaceRequest race; // the race being set up

    // The music's play lists (the program's MPlayList, 0x437E50 .. 0x4382B0; the option object's
    // `bgm_playlist`, the races' music, and `slide_playlist`; saved in the option block,
    // docs/formats/save.md "The play lists"): a row a piece of the streamed `racing` group
    // (its count in `rows`), each enabled or not, its place in the playing order, played this
    // round or not; the position in the order, shuffled or not, the shuffle's seed.
    struct PlayList {
        struct Row {
            u64 kept = 0; // the row's first 8 bytes, as loaded (❓ not named)
            u8 played = 0, enabled = 1;
            gt3r::s8 order = -1, order2 = -1; // the place in the playing order (-1 none); its copy (the scripts' [3])
            u32 kept_tail = 0;
        };
        u32 rows = 0; // 0 until the menus fill the list: then the racing group's pieces
        u16 position = 0;
        u8 shuffle = 0, none = 1; // shuffled; no row enabled
        u16 seed = 0;
        u32 kept = 0; // the header's last word
        std::array<Row, 128> row{};
    };
    PlayList bgm_playlist, slide_playlist;

    // MOption's attributes by name. `option_defaults` fills them as a new game has them.
    std::map<std::string, OptionValue> options;
    void option_defaults(const std::string& region);
    s32 option_int(const std::string& name, s32 fallback = 0) const;
    std::string option_text(const std::string& name) const;
    // The units the player chose: what a value in the game's own unit is multiplied by, and
    // the unit's name as the menus write it.
    struct Unit {
        float factor;
        const char* name;
    };
    Unit velocity_unit() const; // of km/h
    Unit power_unit() const;    // of PS
    Unit torque_unit() const;   // of kgf.m
    // The options as text lines ("name = value"), and back; unknown names are kept.
    std::string options_text() const;
    void options_from_text(const std::string& text);

    // Gran Turismo mode: the program's Status object as it is saved (docs/formats/save.md).
    s64 money = 0; // a new game's 10,000 are the scripts'
    std::string username, password, last_entry_name;
    s64 score = 0, total_prize_money = 0;
    u32 total_prize_cars = 0;
    std::array<u32, 7> rank_counts{}; // 0: races not finished; 1 .. 6: finishes in that place
    s64 aspec_distance = 0, bspec_distance = 0;
    // The B-spec driver's book (UserProfile +0x9C, Status +0x3E4; game_bspec.h): three tables of 8 x 8 u16, its
    // experience by the course's and the car's class.
    std::array<u16, 3 * 64> bspec_book{};
    int withdrawn = 0, withdrawn_gt4p = 0, met_type = 0;

    // The calendar: today's day number and the diary, one event a past day, the latest last.
    static constexpr u32 kFirstDay = 2'453'462; // 2005-04-02, a new game's first day
    static constexpr size_t kDiaryDays = 2922;
    u32 today = kFirstDay;
    std::deque<DayEvent> diary;
    void put_day(const DayEvent& e); // a day passes with this event

    // The garage: its list, the car driven, the key and serial that tie the saved files.
    std::vector<GarageEntry> garage;
    int riding_car = -1; // the list's place; -1: none
    bool riding_fresh = false; // the car driven was changed to one never driven (its record made as sold)
    u32 garage_key = 0, garage_serial = 0;
    std::array<s32, 4> garage_kept{}; // the four words after the key (not named)
    int garage_count() const;
    int add_car(GarageEntry e); // the first free place; -1 when full
    static constexpr size_t kGarageSize = 1000;

    // Records: a race's result by its unit (the RACE table's place of its label), the license
    // tests' and the courses' rankings.
    std::array<u32, 768> race_units;
    Bytes license_record, course_record, machine_record;
    // Gran Turismo 4 Prologue's license units as its save holds them (48 x 0x2E0; save/prologue_data.h); empty: none.
    Bytes prologue_license;

    // Arcade's unlocked cars and courses (labels), favorites (labels, in order), presents given,
    // used cars sold this week, the championship left unfinished.
    std::set<std::string> available_cars, available_courses;
    bool all_available = false;
    std::vector<std::string> favorite_cars, favorite_courses;
    static constexpr size_t kFavoriteLimit = 30;
    std::array<u32, 29> presents{};
    std::array<u32, 8> used_sold{};
    u32 used_week = 0;
    std::string suspended_racecode;
    int suspended = 0, suspended_session = 0;
    Bytes championship_rest; // the context's bytes past the named ones (0x88 in all)
    // The menus' place kept in the save (Context: the major project and page, the menu stack).
    int saved_menu_depth = -1; // the kept stack's length in bytes (-1: none kept)
    Bytes saved_menu_stack;

    // The screen's figures the option's monitor_size setter copies from the program's tables
    // (0x436D60: Option +0x24 .. +0x4F, 0x2C bytes; the 3D view's monitor and aspect). Empty:
    // not set (a new game's constructor leaves zeros).
    Bytes monitor_params;
    // Everything saved that is not named above, as the last loaded game had it (a new game: as
    // the program's constructors make it). The save module writes the named fields over it.
    Bytes saved_image;

    // Something to save (MGame.isNeedSave answers it and clears it, 0x43AC68): a day put in the
    // diary sets it (0x42F0E0), as does setNeedSave.
    bool need_save = false;
    bool during_quick_menu = false;

    // A race sequence (the menus left for a race, until they are back): what its result routines
    // keep between its runs. A sequence starts when the scripts that asked for it finish.
    struct RaceSession {
        int best_place = 15; // the best place finished in it (an event's record follows a better one only, 0x114498)
        std::string sequence; // the sequence asked for ("" : none under way)
        // A license test's sequence puts its day in the diary when it ends (0x110800): the last
        // run's result and time, or a day with nothing in it when no run passed (result 13).
        bool license_open = false;
        DayEvent license_day; // type 0 until a run
    } race_session;
    // The race sequence is over (the menus go on): what the program writes at its end.
    void end_race_sequence();
    bool sequence_asked = false; // next_sequence was set by the scripts since the last project
    // A championship under way (the program's series object, 0x3EEFE0 on): its label, the races
    // run of its count, and its entrants (0 the player's): the car's code, its paint, the points.
    struct Series {
        std::string label;
        int races_run = 0, race_count = 0;
        struct Entrant {
            u64 code = ~u64(0);
            s32 paint = 0, points = 0;
        };
        std::vector<Entrant> entrants;
    } series;
    // The championship is left in the middle (keep) or closed (0x119BD8): a kept one is saved
    // in the game's championship block and taken up again from it.
    void suspend_series(bool keep);

    GameState();
    void new_game(); // the constructors' values: no car, no records, the first day
    // The memory card as the menus see it. A console with no card answers "not connected";
    // with an empty card, "no file".
    struct MemoryCard {
        bool connected = true;
        bool formatted = true;
        bool has_game_data = false; // a saved game on it
        int free_kb = 8000;
    } card;
    bool valid_game_data = false; // MMemoryCardManager::setExistValidGameData: a game was loaded or made
    // The card manager's action (setAction: 1 a replay loaded to be shown, 0 none) and the replay
    // file its last load read (the Replay Theater, the program's load_replay sequence 0x10CA50).
    int card_action = 0;
    std::string replay_file;
    // The race's menus' shared object (MQuickWork): race_natives.h.
    std::shared_ptr<QuickWork> quick_work;
};

// A unit by the option's name ("MPH", "HP", "FTLB", ...): what a value in the game's own unit
// (km/h, PS, kgf.m) is multiplied by, and the unit's name as the menus write it.
GameState::Unit velocity_unit_of(const std::string& option);
GameState::Unit power_unit_of(const std::string& option);
GameState::Unit torque_unit_of(const std::string& option);

// The car a race takes from the garage (RaceRequest::garage_slot): the sheet in use of its
// record, the program's car equipment record as it is (docs/formats/save.md "The car
// record"). Nothing for a place without a driven car's record.
std::optional<sim::Equipment> race_equipment(const GameState& game, int garage_slot);

} // namespace gt4r::menu
