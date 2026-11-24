#pragma once

// The pit stops on a race of ours: the program's pit pass, request and stop timing (pit_flow.h) run for every car each
// step, the drivers sent down the pit lane, onto the stop line, held in the box and out again (Field's pit steps), and
// what a stop changes asked of the car's owner (the session's fuel and tires) through hooks.
//
// For the race screen, per step:
//   pits.before(field, inputs);     // after field.drive(inputs): a car standing in its box held (no throttle, the brake on)
//   session.step_once(inputs);
//   pits.after(session, field);      // the request, the pass (the drivers' line steps), the stop's timing
// and B-spec's pit command (Circle, command 6: F +0x4B1 bit 3): pits.request(car).

#include "gt4r/race/simulation.h"
#include "gt4r/sim/ai/drivers.h"
#include "gt4r/sim/ai/pit_flow.h"

#include <array>
#include <functional>
#include <utility>
#include <span>
#include <vector>

namespace gt4r::sim::ai {

// What a stop needs of the car's owner. Each hook may be left empty: no fuel counted (the fuel never asks for a stop,
// no refuelling), no wear (the tires never ask for one, the change only counted).
struct PitHooks {
    // The fuel: whether it is counted, the fuel left, the tank, the distance since the tank was filled and the fuel used
    // since (F +0x5D0 / car +0x5D4 / F +0x4CC / +0x4C8 / +0x4C4).
    std::function<bool(size_t car)> fuel_on;
    std::function<F(size_t car)> fuel, tank, fuel_distance, fuel_used;
    std::function<void(size_t car, F fuel)> set_fuel;
    // The wear: whether the race wears tires, its limit (conductor + 4 + 0x20), each wheel's tread left (+0x50) and its
    // +0xAC (< 0: the tire gone).
    std::function<bool()> wear_on;
    std::function<F()> wear_limit;
    std::function<std::vector<F>(size_t car)> tread, burst;
    // A wheel given a new tire (0x362E58), at the stop's 60th step from its end.
    std::function<void(size_t car, int wheel)> new_tire;
    // The car's tires as the race keeps them (cond + 0xFB20 + 2 i: front, rear kind), for the stops' record.
    std::function<std::pair<int, int>(size_t car)> tires;
    // The race display's events (0x35E000): 1 the car into the pits, 6 out of them.
    std::function<void(size_t car, int event)> display_event;
    // The car stood in its box (0x363560: the standing start's pose there) facing `heading` (the box's, radians).
    std::function<void(size_t car, F heading)> place_in_box;
};

// The race's part (what the conductor answers): its laps (0: a run without an end), whether it has pit stops (the
// conductor's virtual +0x180), its time limit (minutes, 0 none), and each car's result being in (F +0x4B0).
struct PitRace {
    int laps = 0;
    bool pits = true;
    u32 limit_minutes = 0;
    std::function<bool(size_t car)> finished;
    u8 refuel_rate = 20; // the program's byte 0x620E25 (x 0.1 a second), read by PitStops' constructor
};

class PitStops {
public:
    PitStops(const Field& field, const Program& program, size_t cars, PitHooks hooks = {}, PitRace race = {});

    // B-spec's pit command (0x3F19E0 command 6) or any request: the car goes in at the pit lane's entry.
    void request(size_t car) { if (car < cars_.size()) cars_[car].flags |= 8; }
    // Before the session's step: a car standing in its box held.
    void before(const Field& field, std::span<race::CarInput> inputs);
    // After the session's step.
    void after(const Session& session, Field& field);

    const PitCar& car(size_t i) const { return cars_[i]; }
    PitCar& car(size_t i) { return cars_[i]; }
    // The pit message (F +0x5FA) for the race display: 0x35ACD8's codes.
    int message(size_t i) const { return cars_[i].status; }
    // The stops each car made (0x35AD10: cond + 0xD9E4 + 2 i) and the record of its last four, newest first (cond + 0xD9F0 +
    // 8 i: the lap of the stop, 0x343B50; + 0xDA20 + 8 i: the tires fitted, front and rear, -1 none).
    int stops(size_t i) const { return stops_[i]; }
    struct StopRecord {
        int lap = 0;
        int front = -1, rear = -1;
    };
    const std::array<StopRecord, 4>& stop_record(size_t i) const { return records_[i]; }

private:
    PitFlow flow_;
    PitLine line_;
    PitHooks hooks_;
    PitRace race_;
    std::vector<PitCar> cars_;
    std::vector<F> last_d_;
    std::vector<int> last_lap_, stops_;
    std::vector<std::array<StopRecord, 4>> records_;
    std::vector<gt3r::s8> axle_{1, 1, -1, -1};
};

} // namespace gt4r::sim::ai
