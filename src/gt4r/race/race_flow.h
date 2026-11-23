#pragma once

// A race's own course from the start to the results, as the game's race object runs it: the start's
// hold and its signal, each car's laps and times on the race clock, the order, the finish rule and
// the result rows. It reads the cars' states after each step and nothing else of the simulation.
// docs/formats/race_flow.md.

#include "gt4r/base/types.h"
#include "gt4r/race/simulation.h"

#include <span>
#include <vector>

namespace gt4r::race {

// How a race starts (the race information's start type).
enum class StartType : int {
    Rolling = -1, // the cars run up to the line; no hold, no count
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
};

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
        s64 lap_start_units = 0;
        s64 last_lap_ms = -1, best_lap_ms = -1;
        float prev_v = 0;
        bool has_v = false;
        int prev_lap = 0;
    };
    // What a step brought.
    struct Events {
        bool beep = false;          // a count beep (3, 2 and 1 s before the start)
        bool start = false;         // the start signal
        int lap_done = -1;          // a car that completed a lap this step (the last such, -1 none)
        std::vector<size_t> finished; // cars that finished this step
        bool player_finished = false;
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
    bool holding() const { return hold_ > 0; }
    // Steps of the hold left.
    int hold_left() const { return hold_; }
    int hold_total() const { return hold_total_; }
    // After a step of the simulation (held or not), with the cars' states it left.
    Events step(std::span<const CarState> cars);

    s64 clock() const { return clock_; }
    s64 time_ms() const { return clock_ms(clock_); }
    const CarRecord& car(size_t i) const { return cars_[i]; }
    size_t car_count() const { return cars_.size(); }
    // The cars by their race order now: the finished ones by their finish, then by line crossings
    // and distance along the lap.
    const std::vector<size_t>& order() const { return order_; }
    // A car's place, 1 the leader.
    int place(size_t car) const;
    // The lap the car is on (1 the first), at most the race's laps.
    int lap_shown(size_t car) const;
    // The running lap's time.
    s64 lap_ms(size_t car) const;
    bool player_finished() const { return cars_.empty() ? false : cars_[setup_.player].finished; }
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

    FlowSetup setup_;
    std::vector<CarRecord> cars_;
    std::vector<size_t> order_;
    int hold_ = 0, hold_total_ = 0;
    s64 clock_ = 0;
    int finishers_ = 0;
    int player_finish_steps_ = -1, started_steps_ = -1;
};

} // namespace gt4r::race
