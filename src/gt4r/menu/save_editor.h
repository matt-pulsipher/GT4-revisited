#pragma once

// What the save editor changes in a saved game (docs/SAVE_EDITOR.md): a game's state run behind the menus' own natives, with
// Gran Turismo mode's scripts and text loaded and no page shown, so that a car is made, tuned and priced, an event's list, its
// prize car and the completion are worked out by the code the game itself uses. Nothing here plays a race or writes a file
// (save/save_edit.h does that).

#include "gt4r/menu/runtime.h"

#include <array>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace gt4r::db {
struct CarEntry;
}

namespace gt4r::menu {

struct CarHandle;
struct GameNatives;

class SaveEditor {
public:
    // `config`: as the menus' (its data folder is not used: nothing is loaded or kept). Throws as Runtime does.
    SaveEditor(FileReader read, RuntimeConfig config);
    ~SaveEditor();
    // The game to edit: Gran Turismo mode's project is loaded for its options (its zone's lists), then the state put in.
    void start(const GameState& loaded);
    GameState& state() { return rt_->game(); }
    const GameState& state() const { return rt_->game(); }
    Runtime& runtime() { return *rt_; }

    // ---- Career
    static constexpr s64 kMaxMoney = 999'999'999;
    static constexpr int kMaxDays = 9'999;
    static constexpr u32 kMaxCount = 99'999;
    static constexpr size_t kNameLetters = 10; // the name page's
    void set_money(s64 v) { state().money = std::clamp<s64>(v, 0, kMaxMoney); }
    void set_name(const std::string& v); // letters (and digits), at most ten; an empty one is left out
    int days() const { return int(state().today - GameState::kFirstDay); } // days gone by
    // The calendar moved on (each day passed a day with nothing in it, as the calendar's incDate) or back (the days after it
    // taken out of the diary, which holds a day for each day gone by).
    void set_days(int v);
    int completion(); // per mille
    int aspec_points();
    // The B-spec driver (game_bspec.h): the Status page's figures, and the experience its book holds.
    struct BSpec {
        int skill = 0, machine = 0, course = 0, battle = 0;
    };
    BSpec bspec();
    static constexpr int kCarClasses = 8;
    int bspec_class_level(int k) const; // 0 .. 100 % of what the figures count
    void set_bspec_class_level(int k, int percent);
    int bspec_course_level(); // 0 .. 100 %: the courses' marks against every counted course's most
    void set_bspec_course_level(int percent);
    // Races run and finished in each place.
    void set_finishes(int place, u32 n); // 0: not finished; 1 .. 6
    // What may be brought over from other games, the ending.
    bool ending_seen() const { return state().option_int("can_watch_ending") != 0; }
    void set_ending_seen(bool on) { state().options["can_watch_ending"] = s32(on); }
    std::string championship_under_way(); // its name; "" : none
    void abandon_championship() { state().suspended = 0; }

    // ---- Licenses
    enum Medal { kNone, kBronze, kSilver, kGold };
    struct LicenseTest {
        std::string label, name; // "l0b0001", "Acceleration and Braking: 1"
        int unit = -1;           // its race record
        int ranking = -1;        // its place among the license rankings
        u32 limit[4] = {};       // the medals' times in ms by Medal (0: none)
    };
    struct License {
        std::string category, name; // "l0b", "B License"
        std::vector<LicenseTest> tests;
    };
    const std::vector<License>& licenses() const { return licenses_; }
    Medal medal(const LicenseTest& t) const;
    u32 best_time(const LicenseTest& t) const; // kNoTime: none
    // A medal set: the test's record takes it; its ranking loses the times that would earn a better one, and takes the medal's
    // own time when no time left earns it. None: never passed, the ranking empty.
    void set_medal(const LicenseTest& t, Medal m);
    // The best time set (within the bronze time), with the medal it earns.
    bool set_time(const LicenseTest& t, u32 ms);
    bool license_held(const License& l); // the game's own rule

    // ---- Events
    struct EventRace {
        std::string label, name; // its course's name
        int unit = -1;
    };
    struct Event {
        std::string code, name, level; // "am_sunday_0000", "Sunday Cup", "Easy" ("" : none)
        bool championship = false;
        int unit = -1; // a championship's own record
        std::vector<EventRace> races;
        std::string prize; // the prize car's label ("" : none)
        int prize_paint = -1;
        bool prize_ending = false; // the game's ending comes with it
    };
    struct EventList {
        std::string title;
        std::vector<Event> events;
    };
    const std::vector<EventList>& events() const { return events_; }
    static constexpr int kNotRun = -1, kOther = -2;
    // The best place (0 first .. 5; kNotRun; kOther: a result that is no place).
    int place(int unit) const;
    bool entered(const Event& e) const;
    bool won(const Event& e) const; // a championship's own record, or every race
    // A race's best place, and what the map shows; with kNotRun the race as never run. (The A-spec points stay with a place.)
    void set_place(const Event& e, int race, int place);
    void set_series(const Event& e, int place); // a championship's own record
    void set_won(const Event& e, bool won);     // every race of it first (and the championship won), or never run
    void win_all(bool won);
    int score(int unit) const { return unit < 0 ? 0 : int(state().race_units[size_t(unit)] >> 16 & 0xFF); }
    void set_score(int unit, int points);
    static constexpr int kMaxScore = 200; // a race's most
    // The prize car waits for the next visit to Gran Turismo mode (the present page's own test).
    bool prize_waiting(const Event& e) const;
    void set_prize_waiting(const Event& e, bool on);
    void all_prizes_waiting(); // of every event won

    // ---- Missions
    struct Mission {
        std::string label, name, course;
        int unit = -1;
    };
    const std::vector<Mission>& missions() const { return missions_; }
    void set_mission(const Mission& m, int place); // kNotRun, 0 ..

    // ---- Presents (the present page's list): licenses', missions' and completion's cars and papers
    struct Present {
        int id = 0;
        std::string how, what; // "B License", all gold; the car's name
        std::string car;       // its label ("" : a paper or a movie)
        std::string kind, about;
        int paint = -1; // -1: any
        bool ending = false;
    };
    const std::vector<Present>& presents() const { return presents_; }
    bool present_given(const Present& p) const;
    void set_present_given(const Present& p, bool on);
    bool present_earned(const Present& p); // and not given
    // The presents earned and not given, taken at once as the present page gives them (a day passes for each car); the ending's
    // car and movie are left for the game to show. The cars got; `full`: the garage was full for some.
    int receive_presents(bool* full = nullptr);
    int presents_waiting(); // how many that would give

    // ---- Records
    struct CourseRecord {
        std::string label, name;
        size_t at = 0; // in the course records (or the machine tests')
        bool machine = false, speed = false;
        struct Entry {
            u32 value = 0; // ms, or a float's bits (m/s)
            std::string car, date;
        };
        std::vector<Entry> entries;
        int marks = 0; // the B-spec driver's, 0 .. 100 %
    };
    std::vector<CourseRecord> course_records();
    void delete_entry(const CourseRecord& r, size_t entry);
    void clear_record(const CourseRecord& r);

    // ---- Arcade, courses
    struct Course {
        std::string label, name;
    };
    // The courses that can be opened (the event data's lists, then photo travel's places).
    const std::vector<Course>& courses() const { return courses_; }
    bool course_open(const Course& c) const { return state().available_courses.count(c.label) > 0; }
    void set_course_open(const Course& c, bool on);
    void open_all_courses(bool on);
    size_t cars_open() const { return state().available_cars.size(); }
    void open_all_cars(bool on); // off: the garage's cars only
    void set_car_open(const std::string& label, bool on);
    // Used cars sold this week back on sale.
    int used_sold() const;
    void restock_used() { state().used_sold = {}; }

    // ---- The garage
    struct CarKind {
        std::string label, name, maker;
        int year = 0;
        u32 price = 0;
    };
    const std::vector<CarKind>& cars() const { return cars_; } // every car of the disc, in its order
    std::string car_name(const std::string& label) const;
    struct Paint {
        std::string name;
        u8 rgb[3] = {};
    };
    std::vector<Paint> paints(const std::string& label);
    // A new car as a purchase makes it, open in Arcade. The garage's place; -1 full.
    int add_car(const std::string& label, int paint);
    void remove_car(int slot);
    void set_active(int slot);
    struct Figures {
        int hp = 0, kg = 0; // in the save's power unit; 0: not shown
        std::string power_unit;
    };
    Figures figures(int slot);
    int car_paint(int slot);
    void set_car_paint(int slot, int paint);
    // The odometer in the save's unit (whole); its name.
    u32 odometer(int slot);
    void set_odometer(int slot, u32 units);
    const char* distance_unit() const;
    u32 max_odometer() const; // in that unit
    int got_on_day(int slot) const { return int(state().garage[size_t(slot)].day - GameState::kFirstDay); }
    void set_got_on_day(int slot, int day);
    // The condition. No wear at all: the car's own (race specials), not changed.
    bool no_wear(int slot);
    enum Engine { kNewEngine, kRunIn, kWorn };
    Engine engine(int slot);
    void set_engine(int slot, Engine e);
    int oil_used(int slot); // percent of its life, 0 fresh
    void set_oil_used(int slot, int percent);
    int dirt(int slot); // percent of the most
    void set_dirt(int slot, int percent);
    int body_worn(int slot); // percent
    void set_body_worn(int slot, int percent);
    // Settings sheets.
    int sheet(int slot);
    void set_sheet(int slot, int s);
    // Parts: the kinds this car can have, each with what can be fitted.
    struct PartOption {
        int category = 0;
        std::string name;
        bool owned = false, fitted = false;
    };
    struct PartKind {
        int type = 0;
        std::string name;
        bool permanent = false; // work that is not undone in the game
        std::vector<PartOption> options;
        int fitted = 0; // index
    };
    std::vector<PartKind> parts(int slot);
    void fit_part(int slot, int type, int category); // owned from then on
    void own_part(int slot, int type, int category, bool on);
    void own_all_parts(int slot);
    // The settings the fitted parts allow, as the settings pages show them.
    struct Setting {
        std::string name; // "Ride Height (mm), front"
        s32 value = 0, lo = 0, hi = 0;
        int decimals = 0, offset = 0;
        std::string group;
        std::string setter; // which, for set_setting
        int index = 0;
    };
    std::vector<Setting> settings(int slot);
    void set_setting(int slot, const Setting& s, s32 value);
    std::string setting_text(const Setting& s) const;
    // Wheels by tire category (0 road, 1 racing, 2 dirt, 3 snow) and the wing; "" the car's own.
    struct Extra {
        std::string label, name;
    };
    const std::vector<Extra>& wheels() const { return wheels_; }
    const std::vector<Extra>& wings() const { return wings_; }
    bool wheels_allowed(int slot);
    bool wing_allowed(int slot);
    std::string wheel(int slot, int category);
    void set_wheel(int slot, int category, const std::string& label);
    std::string wing(int slot);
    void set_wing(int slot, const std::string& label);

private:
    std::unique_ptr<Runtime> rt_;
    FileReader read_;
    std::string started_zone_;
    bool started_ = false;
    std::unique_ptr<Project> setting_text_; // the settings pages' text
    std::vector<License> licenses_;
    std::vector<EventList> events_;
    std::vector<Mission> missions_;
    std::vector<Present> presents_;
    std::vector<Course> courses_;
    std::vector<CarKind> cars_;
    std::unordered_map<std::string, size_t> car_index_;
    std::vector<Extra> wheels_, wings_;

    GameNatives& natives();
    script::Value lookup(const std::string& path);
    script::Value call(const script::Value& f, std::vector<script::Value> args = {});
    script::Value method(const script::Value& object, const std::string& name, std::vector<script::Value> args = {});
    script::Value game();
    std::string text(const std::string& page, const std::string& group, const std::string& key);
    std::string setting_label(const std::string& page, const std::string& key) const;
    std::string course_name(const std::string& label);
    u32& unit(int i) { return state().race_units[size_t(i)]; }
    void read_licenses();
    void read_events();
    void read_missions();
    void read_presents();
    void read_courses();
    void read_cars();
    void read_wheels();
    script::Value car(int slot); // the script's car on the garage's record, made when it has none
    CarRecord* record(int slot, bool make);
    void refresh(int slot); // the list's columns after a change of the record
};

} // namespace gt4r::menu
