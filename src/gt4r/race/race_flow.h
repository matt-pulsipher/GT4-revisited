#pragma once

// A race's own course from the start to the results, as the game's race object runs it: the start's
// hold and its signal, each car's laps and times on the race clock, the order, the finish rule and
// the result rows. It reads the cars' states after each step and nothing else of the simulation.
// docs/formats/race_flow.md.

#include "gt4r/base/types.h"
#include "gt4r/race/simulation.h"

#include <span>
#include <string>
#include <vector>

namespace gt4r::race {

// How a race starts (the race information's start type).
enum class StartType : int {
    Moving = -2,  // a license test's moving start: no hold, the clock from the first step, the player's run timed from where
                  // its clock starts (the section's start, else the line)
    Rolling = -1, // the cars run up to the line; no hold, no count, the clock from the first car's crossing
    Given = 0,    // a hold of the race's own number of seconds
    Eight = 1,    // a hold of 8 s with the opening shots
    Ten = 2,      // a hold of 10 s with the opening shots
    None = 3,     // no hold
};

struct FlowSetup {
    StartType start = StartType::Ten;
    int start_seconds = 0; // for StartType::Given
    int laps = 2;          // 0: no lap count (the race ends otherwise)
    size_t player = 0;     // the player's car
    float lap_length = 0;  // metres (the road's)
    // Prologue's tests (DynamicsConductorLicense's start line 0x3B4C88): the clock runs from the race's first step, each car's
    // times count from its own crossing of the line (F + 0x534, the crossing's moment within its step).
    bool player_clock = false;
    std::vector<bool> past_line; // by car: it starts past the line (its crossings' count 1: 0x3C9838); empty: none
    // The road's split points (the sectors' ends but the last; none on a road section) and the race's goal along the course
    // (-1 none), for where the cars' crossings start. `counted` off: every car starts at 0 (Prologue, not read).
    std::vector<float> sectors;
    float goal = -1;
    bool counted = false;
    // A license test's, training's or mission's (the license conductor's rules): its road section's start (-1 none), each
    // car's crossings and start mark (F + 0x65B) as the set-up leaves them (empty: by start_crossings, 0), the kind of its
    // end (CFG +0x02: 2 at the goal, 3 and 6 at the laps), a mission (its times from the race's start).
    float section_start = -1;
    std::vector<int> crossings;
    std::vector<u8> marks;
    int test_kind = 0;
    bool mission = false;
    bool no_lap = false; // a course without a lap (COURSE +0x12 = 1, K +0xCBDE)
};

// A car's crossings of the line where it stands at the start (the set-up 0x363B10, 0x364300 .. 0x36444C: F + 0x4AC), `v`
// its distance along the course: 1 (past the line) unless it stands in the lap's last sector; there still 1 short of the
// goal or in the lap's first half; else 0 (behind the line).
int start_crossings(float v, float lap_length, std::span<const float> sectors, float goal);

// The race clock: 3000 units a second, 50 a step.
constexpr int kClockPerStep = 50;
constexpr s64 clock_ms(s64 units) { return units / 3; }

// The grid's order: the player takes the last place, the others the places before it in entry order.
// Returns each entry's place (0 the front).
std::vector<int> grid_places(size_t entries, size_t player);

// The hold's length in steps for a start type (0 for none).
int start_hold_steps(StartType type, int given_seconds);

class RaceFlow {
public:
    struct CarRecord {
        int counter = 0;        // line crossings: 0 behind the line at the start, 1 past it, +1 a lap
        int laps_done = 0;      // laps completed
        bool finished = false;
        int finish_order = -1;  // 0 the first to finish
        s64 finish_units = -1;  // the race clock at the finish (the total time)
        s64 base_ms = 0;        // F + 0x614: the moment its times count from (ms)
        s64 start_ms = 0;       // (FlowSetup::player_clock: its crossing of the line, the whole run's)
        u8 mark = 0;            // F + 0x65B: 1 before the section's start, 2 past it
        u8 back = 0;            // F + 0x4B0: backward crossings of the line not yet taken back
        s64 last_lap_ms = -1, best_lap_ms = -1;
        float prev_v = 0;
        bool has_v = false;
    };
    // What a step brought.
    struct Events {
        bool beep = false;          // a count beep (3, 2 and 1 s before the start)
        bool start = false;         // the start signal
        bool clock_started = false; // a rolling start's clock started (the first car took the line)
        int lap_done = -1;          // a car that completed a lap this step (the last such, -1 none)
        std::vector<size_t> finished; // cars that finished this step
        bool player_finished = false;
        std::vector<size_t> started; // cars whose start was counted this step (the conductor's virtual 20: the line first
                                     // taken, or a section's start): let go from a rolling start
    };
    // A row of the results, in finishing order.
    struct ResultRow {
        size_t car = 0;
        bool finished = false;
        s64 total_ms = -1;   // the finish time
        int laps_behind = 0; // laps short of the winner's (a car that did not finish)
        s64 best_lap_ms = -1;
    };

    RaceFlow() = default;
    RaceFlow(const FlowSetup& setup, std::span<const CarState> cars) { begin(setup, cars); }
    void begin(const FlowSetup& setup, std::span<const CarState> cars);

    // Before a step: true while the start's hold runs (the cars are held, the clock stands).
    bool holding() const { return hold_ > 1; }
    // Steps of the hold left (the race information's +0xBC, counted down a frame at a time before the step: the step that
    // takes it to 0 is the first of the race).
    int hold_left() const { return hold_; }
    int hold_total() const { return hold_total_; }
    // After a step of the simulation (held or not), with the cars' states it left.
    Events step(std::span<const CarState> cars);

    s64 clock() const { return clock_; }
    // The race clock runs (a rolling start's from the first car's crossing of the line).
    bool clock_running() const { return clock_running_; }
    s64 time_ms() const { return clock_ms(clock_) - (setup_.player_clock ? cars_[setup_.player].counter == 0 ? clock_ms(clock_) : cars_[setup_.player].start_ms : 0); }
    const CarRecord& car(size_t i) const { return cars_[i]; }
    size_t car_count() const { return cars_.size(); }
    // The cars by their race order now: by crossings of the line, then (equal crossings) a running car
    // ahead of a finished one, finished ones by their finish, running ones by distance along the lap.
    const std::vector<size_t>& order() const { return order_; }
    // A car's place, 1 the leader.
    int place(size_t car) const;
    // The lap the car is on (1 the first), at most the race's laps.
    int lap_shown(size_t car) const;
    // The running lap's time.
    s64 lap_ms(size_t car) const;
    bool player_finished() const { return cars_.empty() ? false : cars_[setup_.player].finished; }
    // A car's run ended by a rule of its own (a license test's stop, a failure) `share` of a step
    // into the step just run: its finish at the clock then.
    void finish_now(size_t car, float share = 0);
    // The player's start signal before the line (a free run's hand-over: event 4 from DynamicsConductorFreeRun::vf14): the next
    // step's Events::start.
    void signal_start() { signal_start_ = true; }
    // A finished car's time: the race's (the clock at its finish), a test's from where its clock started (F + 0x614).
    s64 run_ms(size_t car) const;
    bool everyone_finished() const;
    // Steps since the player finished (-1 before).
    int steps_since_player_finish() const { return player_finish_steps_; }
    // Steps since the start signal (-1 before).
    int steps_since_start() const { return started_steps_; }
    std::vector<ResultRow> results() const;
    const FlowSetup& setup() const { return setup_; }

private:
    float progress(size_t i, const CarState& c) const;
    void sort_order(std::span<const CarState> cars);
    void cross(size_t i, float was, float now, Events& ev);
    void start_car(size_t i, s64 t, Events& ev);
    void end_car(size_t i, s64 at, Events& ev);

    FlowSetup setup_;
    bool signal_start_ = false;
    std::vector<CarRecord> cars_;
    std::vector<size_t> order_;
    int hold_ = 0, hold_total_ = 0;
    s64 clock_ = 0;
    bool clock_running_ = true;
    int finishers_ = 0;
    int player_finish_steps_ = -1, started_steps_ = -1;
};

// The results page's rows as the program keeps them (ResultArcade, docs/formats/race_flow.md "The
// results"): a row per place; a car's row is written for good when it finishes (its time: the
// winner's total, the others' gap to it, or the laps short of it), and while the page is up and
// cars still run, the rows of the running cars follow the race's order (their names, no time).
// Each row has a "changed" mark the page takes once (CB_STABLE).
class ResultBoard {
public:
    struct Row {
        int car = -1;     // the car shown (-1 none yet)
        std::string time; // " 1'23.456", "+0'01.234", "+1Lap"; empty while the car runs
        bool changed = false;
        bool fixed = false; // the car has finished
    };
    // `lap_word` / `laps_word`: the race's words for one lap and several (WordOfLap, WordOfLaps).
    void begin(size_t cars, size_t player, std::string lap_word = "Lap", std::string laps_word = "Laps");
    // The cars that finished in a step, in that order (RaceFlow::Events::finished).
    void finished(const RaceFlow& flow, std::span<const size_t> cars);
    // While the page is up: the running cars' rows from the race's order now.
    void follow(const RaceFlow& flow);
    // The page's question for a row: changed since it last asked (the mark is taken).
    // Every row as changed: the results movie reads them all again (the points pages of a championship).
    void touch();
    bool take_changed(size_t row);
    const std::vector<Row>& rows() const { return rows_; }
    int player_row() const { return player_row_; }
    bool all_fixed() const { return all_fixed_; }

private:
    void put(size_t row, int car);
    void set_player_row(int row);

    std::vector<Row> rows_;
    std::vector<bool> recorded_;
    size_t player_ = 0;
    int player_row_ = 0;
    bool all_fixed_ = false;
    s64 leader_ms_ = 0;
    int leader_laps_ = 0;
    std::string lap_word_, laps_word_;
};

// The results page's time for a row: " M'SS.mmm" for the winner (`plus` false), "+M'SS.mmm" for a gap.
std::string result_time(s64 milliseconds, bool plus);

} // namespace gt4r::race
