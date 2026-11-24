#pragma once

// The pit stops as the program runs them (docs/specs/ai.md "The pits"): when a car asks for the pits (the wear and fuel
// phase 0x353218), the race's per-car pit pass (0x359008 → 0x35B060: down the pit lane, onto the stop line, the stop in
// the box, back out), the service it gets (0x35AF18: tires, fuel), and the stop's own timing (the hold counter, the
// refuelling 0x354D60, the tire change's moments in 0x35C0B8). The routines' arithmetic is the program's (EE floats);
// what they read of the car, the course and the race is given to them, what they change is written back into the
// car's pit record or asked of the caller (the drivers' line steps, the car put in its box, new tires, fuel).

#include "base/ee_float.h"
#include "gt4r/base/types.h"
#include "gt4r/sim/program.h"

#include <array>
#include <functional>
#include <vector>

namespace gt4r::sim::ai {

using gt3r::s8;
using F = gt3r::ee::F;

// A car's pit record: the program's fields of the car's state block (F = car + 0x104).
struct PitCar {
    u8 flags = 0;      // F +0x4B1: bit 0 the line just crossed, 2 service wanted, 3 the pits asked for, 4 on the pit lane
    u8 x4B2 = 0;       // F +0x4B2 ❓ (the car leaves the race through the pits)
    s8 box = 0;        // F +0x4B7: its box, a point of the stop line (line 4)
    u16 service = 0;   // F +0x4B8: 0x1 / 0x2 ❓, 0x4 tires wanted, 0x8 fuel wanted, 0x10 the player's choice asked, 0x20 the
                       // fuel hose on, 0x40 / 0x80 front / rear tires, 0x100 refuelling, 0x200 the tires changed
    u16 hold = 0;      // F +0x4BA: the hold counter (steps left standing)
    u8 x5C1 = 0;       // F +0x5C1: no pit request for a B-spec car ❓
    u8 tire_out = 0;   // F +0x5C2: a tire past its wear or gone this step
    u8 speeding = 0;   // F +0x5C3: on the pit lane over 10 km/h (1) or 40 km/h (2)
    u8 status = 0;     // F +0x5FA: the pit message (0x35ACD8)
    u8 status_time = 0; // F +0x5FB: its time left (steps), the message held while it runs
    u8 pad_state = 0;  // F +0x62E: a pad car's own pit visit: 1 asked, 2 on the pit lane
    s16 x64C = 0;      // F +0x64C: a B-spec car's wait in the box (300 steps) ❓
    u8 tire_front = 0xFF, tire_rear = 0xFF; // F +0x64E / +0x64F: the tires chosen (0xFF none)
};

// What the pass reads of the car and the race at a step.
struct PitInputs {
    F d, d_prev;          // F +0x4A4 (along the course) now and at the last step (car +0x99C)
    F lap;                // the lap's length (0x34C230)
    F speed, side_speed;  // F +0x53C / +0x540 (m/s)
    s8 control = 0;       // F +0x466: 0 the pad, 1 B-spec, 2 the computer
    u8 kind = 0;          // F +0x467: the driver's kind (2 down the pit lane, 3 on the stop line, 4 leaving)
    u8 full_throttle = 0; // F +0x461
    int index = 0;        // F +0x440
    int lap_state = 0;    // 0x343C20: 0 racing, 2 the last lap, 1 the car's race over
    bool contact = false; // the collision body's +0x43C or +0x440 (0x45CC08)
    bool contact_43C = false; // its +0x43C (0x45CC30)
    bool lines = false;   // the course has driver lines (0x34C750)
    bool crossed = false; // 0x358608 counted a crossing of the line this step
    bool race_over = false; // the race's state byte (cond + 0xCBDE) is 2: no pit pass
    bool counting = false;  // the start's count runs (0x34C210): no pit pass
};

// What the pass asks of the caller, in the order the program does it.
struct PitAction {
    enum Kind : u8 {
        Enter,      // 0x3453A0: onto the pit lane (line 3, kind 2)
        Stop,       // 0x345400: onto the stop line (line 4, kind 3)
        Leave,      // 0x345450: off the stop line, down the pit lane (line 3, kind 4)
        Return,     // 0x345228: back onto the race's line (line 6, kind 0)
        EndRun,     // 0x343D78 (3): the car's run ended in the pits
        PlaceInBox, // 0x363560: the car stood in its box (the standing start's pose), its hold 721
        Heading,    // the car turned to its box's heading (F +0x570, the body's axes 0x36C9F8): `value`
        CountStop,  // 0x35AD10: the race counts the car's stop (its pit stops, the order of the stops)
        Recount,    // 0x35ADF0: the race's order updated for a car out of the pits (0x35E000)
        Refuel,     // 0x354D60 (rate 0): a step of refuelling
        Driver,     // 0x463E80: the driver's object told (a computer car met by another on the pit lane)
    } kind;
    F value;
};

// The stop line's places the pass reads (line 4 of the course's driver file, 0x46BDC0): point k's distance (k < 0:
// from the end), and the box's distance and heading (0x46BE20 / 0x46BE70).
struct PitLine {
    std::vector<F> dist, heading; // per point of the stop line, as the race prepared them
    F marker(int k) const;
    F box_dist(int box) const;
    F box_heading(int box) const;
};

class PitFlow {
public:
    PitFlow() = default;
    explicit PitFlow(const Program& program); // the pit messages' times (0x6207E4)

    // 0x359008: the car's pit pass for a step (the line crossing's messages, then 0x35B060). `service` decides a stop's
    // service (0x35AF18); it is called where the program calls it.
    void step(PitCar& c, const PitInputs& in, const PitLine& line, std::vector<PitAction>& out, const std::function<u16()>& service) const;
    // 0x35ACD8: the pit message `code` (held for its time when the table gives one; else only once the last has run out).
    void message(PitCar& c, int code) const;

    std::array<u8, 16> message_time{}; // 0x6207E4 + 8 k

    // 0x35B060 alone.
    void pass(PitCar& c, const PitInputs& in, const PitLine& line, std::vector<PitAction>& out, const std::function<u16()>& service) const;
};

// 0x35C0B8, the stop's own timing (phase 4 while the hold counter runs): the counter held for the player's choice (with a
// B-spec car's wait) and while the fuel still runs in at 90, else counted down; the tires chosen fitted at 645 / 644, every
// wheel of an axle being served new tires at 60 (0x362E58), the tire bits cleared, and at 59 the change done (0x200).
struct HoldEvents {
    bool default_choice = false;   // 0x35BF08: the player's choice made for him (a B-spec car's wait over) ❓
    bool chosen_tires = false;     // at 645: the car's spec rebuilt with the tires chosen (0x3F3B68, 0x361570)
    bool chosen_params = false;    // at 644: its parameters (0x361798)
    std::vector<int> new_tires;    // at 60: the wheels given new tires (0x362E58)
};
// `axle`: each wheel's layout byte (+0x3C + w: > 0 the front, < 0 the rear, 0 either).
HoldEvents pit_hold_step(PitCar& c, const std::vector<s8>& axle);

// 0x354D60 with rate 0: a step of refuelling while the service has 0x100: the fuel (car +0x5D4) + the program's rate (its byte
// 0x620E25 x 0.1 a second) x the step (car +0x54C), up to the tank (F +0x4CC); full: the refuelling over and, with no tires
// still to change, 90 steps more standing.
void pit_refuel(PitCar& c, F& fuel, F tank, F dt, u8 rate_byte);

// The car's need of a stop (the pit part of 0x353218, the wear and fuel phase): what it reads.
struct PitNeed {
    bool allowed = false;   // the conductor's virtual +0x180: the race has pit stops
    bool fuel_on = false;   // F +0x5D0 > 0 (0x354E38)
    F fuel_distance, fuel_used; // F +0x4C8 (metres since the fill) / F +0x4C4 (fuel used since)
    F fuel;                 // car +0x5D4 (0x354E60)
    F d;                    // F +0x4A4
    F lap;
    F pit_marker;           // the stop line's point 1 (0x46BDC0)
    bool wear_on = false;   // the race's wear switch (conductor + 4 + 0x68)
    F wear_limit;           // conductor + 4 + 0x20
    std::vector<F> wear;    // each wheel's wear (+0x50)
    std::vector<F> burst;   // each wheel's +0xAC (< 0: the tire gone)
    bool tire_state_4 = false; // 0x35FA50 answers 4 ❓
    std::vector<u8> damaged;   // each wheel's +0x1AB ❓
    int lap_state = 0;
    s8 control = 0;
};
// The pit flags the phase sets (F +0x4B1 bits 2 and 3, F +0x5C2).
void pit_need(PitCar& c, const PitNeed& n);

// 0x35AF18: a stop's service (F +0x4B8) when the car reaches its box's lane.
struct ServiceInputs {
    bool pits_on = false;    // the race's byte cond + 0xCBE0 is 1
    int lap_state = 0;
    bool fuel_on = false;    // 0x354E38
    bool tires_allowed = false; // the conductor's virtual +0x198 ❓ (a tire change offered)
    bool player_choice = false; // the race's virtual +0x120: the pit menu (for a car not the computer's)
    s8 control = 0;
    u8 layout_kind = 0;      // the car's wheel layout byte +0x32 (4: no stop)
};
u16 service_of(const PitCar& c, const ServiceInputs& s);

} // namespace gt4r::sim::ai
