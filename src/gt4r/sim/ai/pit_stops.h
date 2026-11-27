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
#include <optional>
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
    // The wear: whether the race wears tires, its limit (conductor + 4 + 0x20), each wheel's wear grip (+0x50: below the
    // limit, a stop asked) and its tread wear
    // +0xAC (< 0: the tire gone).
    std::function<bool()> wear_on;
    std::function<F()> wear_limit;
    std::function<std::vector<F>(size_t car)> wear_grip, tread;
    // A wheel given a new tire (0x362E58), at the stop's 60th step from its end.
    std::function<void(size_t car, int wheel)> new_tire;
    // The car's tires as the race keeps them (cond + 0xFB20 + 2 i: front, rear kind), for the stops' record.
    std::function<std::pair<int, int>(size_t car)> tires;
    // The race display's events (0x35E000): 1 the car into the pits, 6 out of them.
    std::function<void(size_t car, int event)> display_event;
    // The car stopped in its box (0x363560: the standing state where it is), `heading` the box's (radians); and turned to it at the
    // stop's next step (F +0x570).
    std::function<void(size_t car, F heading)> place_in_box, face_box;
    // The stop's hold counter (F +0x4BA) handed to the session each step (0 out of a stop): the session holds the car while it
    // runs (Session::set_pit_hold). Without it the car is braked in its box instead.
    std::function<void(size_t car, u16 hold)> hold;
    // The hold counter the car's set-up left (a pit start's, start kind 6: 120): the stop's counter starts from it.
    std::function<u16(size_t car)> start_hold;
    // The pass's flags (F +0x4B1) handed to the session after each step.
    std::function<void(size_t car, u8 flags)> flags;
    // F +0x62E as the session keeps it (the hits' penalty mark), read before the pass and written back after it (pad_state).
    std::function<u8(size_t car)> mark;
    std::function<void(size_t car, u8 value)> set_mark;
    // The tire change (0x35C0B8) with a kind chosen below 0xFE (F +0x64E / +0x64F, 0xFF none, 0xFE as fitted): at the stop's
    // 645th step from its end the chosen kinds fitted and the car's tire records rebuilt for the axles served (1 front, 2
    // rear, 3 both), at the 644th their base grips (sim/ai/pit_tire_change.h fills them from a session).
    std::function<void(size_t car, u8 front, u8 rear, u8 axles)> change_tires;
    std::function<void(size_t car, u8 axles)> retune_tires;
};

// The hooks filled from a session (its fuel, tank, the economy readings, the wear rules and each wheel's wear, new tires):
// the program's reads of the car (0x353218, 0x354D60, 0x362E58), the box's stop (Session::stop_in_box, face) and its hold (Session::set_pit_hold).
PitHooks session_pit_hooks(Session& session);

// The race's part (what the conductor answers): its laps (0: a run without an end), whether it has pit stops (the
// conductor's virtual +0x180), its time limit (minutes, 0 none). (Each car's backward crossings, F +0x4B0, come from the
// session: Session::back_crossings.)
struct PitRace {
    int laps = 0;
    bool pits = true;
    u32 limit_minutes = 0;
    bool pit_menu = true; // the race's pit menu (its virtual +0x120) for the player's and the B-spec car (Gran Turismo mode)
    u8 refuel_rate = 20; // the program's byte 0x620E25 (x 0.1 a second), read by PitStops' constructor
    u8 mode = 0;         // the conductor's mode (K +0xCBDC): 0 a race, 1 a B-spec race, 2 a B-spec race's replay
};

class PitStops {
public:
    PitStops(const Field& field, const Program& program, size_t cars, PitHooks hooks = {}, PitRace race = {});

    // B-spec's pit command (0x3F19E0 command 6) or any request: the car goes in at the pit lane's entry.
    void request(size_t car) { if (car < cars_.size()) cars_[car].flags |= 8; }
    // The pit command as the program takes it (0x3F19E0 command 6): not before the car's first crossing nor once its laps are run
    // (0x343C20); on, the request; off, taken back (a request passed on to the lane left: F +0x5C1). SCPS-17001 v1.05's
    // (0x3EA7E8): at any time, and taken back without the mark.
    void command(size_t car, bool on) {
        if (car >= cars_.size()) return;
        PitCar& c = cars_[car];
        if (japan_) {
            if (on) c.flags |= 8;
            else c.flags &= u8(~8);
            return;
        }
        if (last_lap_[car] < 1 || lap_state_[car] != 0) return;
        if (on) {
            if (!(c.flags & 8)) c.flags |= 8, c.x5C1 = 0;
        } else if (c.flags & 8) {
            c.flags &= u8(~8);
            if (c.flags & 4) c.x5C1 = 1;
        }
    }
    // The pit menu's choice (the player's, or B-spec's before its wait runs out): front / rear tire kind (0xFF none, 0xFE
    // the kind fitted), fuel (0 none); command 2 stops the refuelling. Taken at the next step's input (0x35BFE8).
    void choose(size_t car, u8 front, u8 rear, u8 fuel, u8 command = 1) {
        if (car < cars_.size()) pending_[car] = Choice{command, front, rear, fuel}, default_choice_[car] = true;
    }
    // A car driven by a recording: its choices come from its records alone.
    void set_recorded(size_t car) {
        if (car < cars_.size()) recorded_[car] = true;
    }
    // A B-spec car's wait ran out in the last step: the choice made for it (a recording's record has it).
    bool took_default(size_t car) const { return car < cars_.size() && took_default_[car]; }
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

    // Whether a car is on the pit's stretch of the course (0x35AE58): between the stop line's second point and its last.
    bool in_stretch(size_t car, float lap) const;

private:
    PitFlow flow_;
    PitLine line_;
    PitHooks hooks_;
    PitRace race_;
    bool japan_ = false; // SCPS-17001 v1.05's program
    std::vector<PitCar> cars_;
    std::vector<F> last_d_;
    std::vector<int> last_lap_, stops_, lap_state_;
    std::vector<std::array<StopRecord, 4>> records_;
    std::vector<gt3r::s8> axle_{1, 1, -1, -1};
    struct Choice {
        u8 command = 1, front = 0xFE, rear = 0xFE, fuel = 0xFE;
    };
    std::vector<bool> default_choice_; // a choice to take at the step's input (a B-spec car's wait ran out: 0x35BF08)
    std::vector<std::optional<Choice>> pending_;
    std::vector<bool> took_default_, recorded_; // the last step's wait ran out; a car driven by a recording
};

} // namespace gt4r::sim::ai
