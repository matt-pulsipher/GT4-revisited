#pragma once

// The computer drivers (docs/specs/ai.md): per car, from its state and the others' to a race::CarInput each
// step. They follow the course's driving lines at the speeds the car's tires allow, brake for the arcs, keep
// apart from and pass the cars around them, and turn round or back out after a spin or a wall.
//
//   ai::Field field = ai::Field::open(disc, "tsukuba", session, {1, 2, 3, 4, 5});  // cars 1.. are the computer's
//   std::vector<race::CarInput> in(session.cars().size());
//   while (countdown) { field.hold(in); session.hold_once(in); }
//   for (;;) { /* in[0] = the player's pad */ field.drive(in); field.apply_time_scales(session); session.step_once(in); }
//
// The drivers read the cars through Session's public interface; they write only the entries of the cars
// they drive.

#include "gt4r/formats/course/ai_line.h"
#include "gt4r/race/simulation.h"
#include "gt4r/sim/ai/course_lines.h"
#include "gt4r/sim/ai/tables.h"
#include "gt4r/sim/session.h"

#include <array>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace gt4r {
class Disc;
}

namespace gt4r::sim::ai {

// A driver's parameters (0x466C20: the conductor's per-car block, from the course's set for the car's
// driveline class, then the event's changes 0x467148).
struct Params {
    using F = gt3r::ee::F;
    F brake{1.0f};           // +0x00 the braking factor: above 1 brakes later
    F corner_scale{1.0f};    // +0x04 the arcs' speeds' scale
    F corner_offset;         // +0x08 and offset (m/s)
    F yaw_gain{2.0f};        // +0x0C the wanted slip angle per rad/s of yaw rate short
    F sat_throttle{1.0f}, sat_brake; // +0x10 / +0x14 the throttle cut and brake past the tires' peak
    F look_speed;            // +0x18 1 / the speed of the longest look ahead
    F look_min{15.0f}, look_max{40.0f}; // +0x1C / +0x20 the look ahead at rest and at that speed (m)
    F front_weight{1.0f};    // +0x58 the front axle's share of the arcs' grip
    F corner_bonus;          // +0x5C added to an arc's speed after bonus_ticks in it (m/s)
    F steer_rate{1.57f};     // +0x60 the steering's slew (rad/s)
    F pit_speed{22.2f};      // +0x64 (m/s)
    int bonus_ticks = 0;     // +0x68
    F throttle{1.0f};        // +0x6C the event's throttle share
    F downforce;             // +0x70 the lift coefficients over the mass
    F aero_bonus;            // +0x74
    bool straight_bonus = false; // +0x78 the arcs' speed raised while the heading error is small
    F bonus_angle, bonus_speed;  // +0x7C (rad) / +0x80 (m/s)
    std::array<F, 3> grip{F(1.0f), F(1.0f), F(1.0f)}; // +0xA8 the arcs' grip by tire class
    gt3r::sim::exact::Aids aids;                      // +0x24 .. the driving aids (rules::driver_block)
};

// The race's own settings for its computer drivers.
struct Pace {
    // The event's percents per driveline class (RACE row: throttle, braking, arcs' speeds; 0: 100).
    std::array<u8, 4> throttle{}, brake{}, corner{};
    // The rubber band (RACE row): the cars ahead of the reference slowed, those behind hurried, by up to these shares
    // past `start` metres, fully past `full` metres. All 0: none.
    float slow = 0, slow_start = 0, slow_full = 0, fast = 0, fast_start = 0, fast_full = 0, global = 1;
    // The same as the event row has them (+0x63 .. +0x6A: the overall percent (0: 100), the mode, the slowing's percent,
    // start (m) and full gap (x 10 m), the hurrying's): what the race sets the band from (0x365480). All 0: no band.
    u8 band_global = 0, band_mode = 0, slow_pct = 0, slow_start_m = 0, slow_full_10m = 0, fast_pct = 0, fast_start_m = 0, fast_full_10m = 0;
    bool standing_start = true; // the long look ahead off the grid of a standing start
    // Per grid place, the seconds its car is held at the start (RACE +0x91, +0x8C .. +0x90; the decision's long look
    // ahead lasts that much longer).
    std::array<s16, 32> start_delay{};
    // The race lets the cars follow and avoid each other (CFG +0x02 != 6).
    bool contact = true;
    // A run alone: a free run, a license test or a machine test (the conductor's virtual 6, its vtable's +0x30, answers 1
    // for DynamicsConductorFreeRun, License and MachineTest, 0 for the races, battles, free practice, training and
    // missions). Its computer drivers do not pass (0x3F1890) and a B-spec driver's experience counts no company (0x4684A8).
    bool solo = false;
};

// What a driver did, for checks and tools.
struct Incidents {
    int wall_hits = 0;     // steps that began a wall contact
    int car_hits = 0;      // steps that began a contact with another car
    int spins = 0;         // times the heading turned more than 80 degrees off the line (recovery state 2)
    int off_road = 0;      // times all four wheels left the road's paved or the course's own surface
    int lane_changes = 0;
    float stuck_seconds = 0; // time spent slower than 5 km/h after the start
};

// The cars as the drivers read them: a session's, or (for checks) another race's.
class Cars {
public:
    virtual ~Cars() = default;
    virtual size_t size() const = 0;
    virtual const gt3r::sim::exact::Car& record(size_t i) const = 0;
    virtual const race::CarState& state(size_t i) const = 0;
    virtual const Spec& spec(size_t i) const = 0;
    virtual bool loose() const = 0;
    virtual const RoadGround& ground() const = 0;
    // Gran Turismo 4's part of a car's record (its tires' records by surface, the front's mean slip angle).
    virtual const sim::Extra& extra(size_t i) const = 0;
    // The collision step's front and rear flags of a car this step (false: not known, the drivers judge them from the
    // walls and the other cars).
    virtual bool hits(size_t, bool& front, bool& rear) const {
        front = rear = false;
        return false;
    }
    // What the program keeps per car that a session has no place for (false: not known, the field keeps its own):
    // the filtered lateral acceleration F +0x560, the speed along the course F +0x650 (m/s), the place in the race
    // F +0x4AF, and the race's clock (the conductor's +0xF89C, 50 a step).
    virtual bool lat_filter(size_t, gt3r::ee::F&) const { return false; }
    virtual bool course_rate(size_t, gt3r::ee::F&) const { return false; }
    virtual bool rank(size_t, int&) const { return false; }
    virtual bool clock(u32&) const { return false; }
    // A car that has finished the race (F +0x4B2).
    virtual bool finished(size_t) const { return false; }
};

// A session's cars.
class SessionCars final : public Cars {
public:
    explicit SessionCars(const Session& s) : s_(&s) {}
    size_t size() const override { return s_->cars().size(); }
    const gt3r::sim::exact::Car& record(size_t i) const override { return s_->record(i); }
    const race::CarState& state(size_t i) const override { return s_->cars()[i]; }
    const Spec& spec(size_t i) const override { return s_->spec(i); }
    bool loose() const override { return s_->loose(); }
    const RoadGround& ground() const override { return s_->ground(); }
    const sim::Extra& extra(size_t i) const override { return s_->gt4_record(i); }
    // The collision step's front and rear flags of the car's body (+0x434 / +0x438) as the last step left them.
    bool hits(size_t i, bool& front, bool& rear) const override {
        front = s_->body(i).front != 0, rear = s_->body(i).rear != 0;
        return true;
    }

private:
    const Session* s_;
};

class Field {
public:
    // The drivers of `computer`'s cars of a session on a course whose driver file is `data`.
    Field(const Session& session, const course::AiData& data, const Program& program, std::span<const size_t> computer, const Pace& pace = {});
    // The same over any cars (kept by the caller while the field lives).
    Field(const Cars& cars, const course::AiData& data, const Program& program, std::span<const size_t> computer, const Pace& pace = {});
    ~Field();
    // With the course's driver file read from the disc (crs/<course>.ad). Throws std::runtime_error without one.
    static std::unique_ptr<Field> open(Disc& disc, const std::string& course, const Session& session, std::span<const size_t> computer,
                                       const Pace& pace = {});
    // The file of a course (crs/<course>.ad), nullopt when the disc has none.
    static std::optional<course::AiData> read(Disc& disc, const std::string& course);

    // The inputs during a start's countdown: the engines held around their shift speed.
    void hold(std::span<race::CarInput> inputs);
    // The inputs for the next step (call once per Session step, before it).
    void drive(std::span<race::CarInput> inputs);
    // drive() in its parts: the step's common part (the order, the rubber band, the pairs pass), then each driven car's
    // decision, then the step's end.
    void begin_step();
    void decide_car(size_t car, race::CarInput& input);
    void end_step();

    // The player's own car (control source 0) keeps a driver that plans as any other but does not drive (the program runs
    // its decision 0x465078 each step and the pad's input after it): drive() leaves its input as the caller set it while its
    // kind is 0. Call before the first step for a car in `computer`. (Field::finish(car, mode, true) hands it to the driver.)
    void set_shadow(size_t car);
    // The suggested gear's speed (0x469E58, each step after the decision, for every driven car): the driver's target speed
    // (D +0x28; mode 4 the lesser of it and D +0x24) when the car is at or above it, else none (D +0x40 = -1). The gear is
    // the gearbox's lowest whose up-shift rpm less 500 (2,000 in first) reaches that speed (0x36BF68, the session's).
    // `firm`: modes 3 / 4 (D +0x41).
    struct Suggestion {
        bool on = false;
        gt3r::ee::F speed;
        bool firm = false;
    };
    Suggestion suggestion(size_t car) const;
    void set_bspec(size_t car);
    // The B-spec driver's skill (0x468C50, bspec_skill.h): its arcs', braking and alongside multipliers (D +0xB8 / +0xBC /
    // +0xC0), set at the race's start after set_bspec.
    void set_bspec_skill(size_t car, gt3r::ee::F corner, gt3r::ee::F brake, gt3r::ee::F alongside);
    // The B-spec driver's record of the race (car +0x8C, 0x40 bytes: bspec_skill.h's BSpecRecord) as the race starts with it
    // and the car's class (bspec_class of the parameter block's +0xFE0); during the race it gains experience (0x4684A8):
    // at each new segment of an arc taken within 30 km/h of its speed, in the speed bins of table a; at each braking point,
    // of table b; at each lane change of traffic, of table c; and each time, d[class / 2] + 0x1000 alone (or in a run alone:
    // Pace::solo), or + 0x555 and e[class / 2] + 0xAAA with a car in the driver's cells.
    void set_bspec_record(size_t car, const std::array<u8, 0x40>& record, int car_class);
    std::array<u8, 0x40> bspec_record(size_t car) const;
    // The driving line's heading at the car's distance along the course (0x4649F0 on the driver's line and its next point),
    // for the steering's active aid (sim-step). 0 for a car not driven.
    gt3r::ee::F line_heading_at(size_t car) const;
    // The car's driver's mode (D +0x1E), beside it: the active steering runs in mode 0.
    u8 line_mode_at(size_t car) const;
    // The car closing in on another fastest, for the on-board camera (0x34AA88 over the threat record D +0x53 / +0x60 /
    // +0x64 the pairs pass keeps, 0x469888): the car it closes on (the least gap / closing speed among those it closes on,
    // else the nearest of those it does not), the gap (m) and the speed between them as the camera takes it (m/s: the
    // closing speed negated, negative while the car closes in). The camera's own test (0x3FD528) is a gap under 20 m. None
    // in a race without contacts (CFG +0x02 = 6) or when no car ahead was near this step.
    struct Threat {
        int car = -1;
        gt3r::ee::F gap, speed;
    };
    std::optional<Threat> threat(size_t car) const;
    // A B-spec command (0x3F19E0): the pace level 0 .. 4 (the program's five: 2 the even pace, 0 the slowest, 4 the
    // fastest; -1 unchanged) and passing (0 off, 1 on, -1 unchanged). The driver keeps both until the next command; passing
    // turned off sends a driver on a passing line back to the main line.
    struct BSpecCommand {
        int pace = -1;
        int overtake = -1;
    };
    void command(size_t car, const BSpecCommand& c);
    int pace_level(size_t car) const;  // the B-spec driver's level (D +0x6D)
    bool overtaking(size_t car) const; // passing allowed (D +0x6E)
    // The B-spec driver's shift speeds: the gearbox's up- and down-shift speeds x this / 128 (0x3F1E30; 128 for any other
    // driver, and for a car with a manual gearbox).
    u8 shift_factor(size_t car) const;

    // A car's run is over (the conductor's virtual 21, 0x343D78): `mode` the race's way of ending it: 0 the computer keeps
    // driving (kind 1: the player's car too), 1 it coasts down and stops (kind 6), 2 it brakes to a stop (kind 7), 3 it
    // cruises at the race's limit or 80 km/h (kind 5). A car on the passing line 7 goes back to the main line. (The race
    // passes its configuration's +0xCBDE when that is 1 and no mode is asked.)
    // The driver's kind after it (8: stopped) is the session's to hold the car by (its clutch open).
    void finish(size_t car, int mode, bool player_car = false);
    int kind(size_t car) const;
    // The car's control source as the drivers keep it (F +0x466: 2 the computer, 1 B-spec, 0 the pad; -1 not driven).
    int control(size_t car) const;
    // The pit's steps for a driven car (the pit pass 0x35B060 decides when: pit_stops.h): onto the pit lane (0x3453A0: line 3
    // at once, kind 2), onto the stop line (0x345400: line 4 at once, kind 3), off it down the pit lane (0x345450: line 3,
    // kind 4), back to racing (0x345228: from kinds 2 .. 4 line 6 blended, kind 0). False when the car has no such line.
    bool pit_enter(size_t car);
    bool pit_stop(size_t car);
    bool pit_leave(size_t car);
    bool pit_return(size_t car);
    // The speed limits of kind 5 (the race's configuration +0x42 for the player's car, +0x43 for the computer's; km/h, above
    // 80 none).
    void set_limits(u8 player_kmh, u8 computer_kmh) { limit_player_ = player_kmh, limit_computer_ = computer_kmh; }

    const CourseLines& lines() const { return lines_; }
    const Tables& tables() const { return tables_; }
    size_t size() const { return drivers_.size(); }
    // Car index -> driver; nullptr for a car the field does not drive.
    struct Driver;
    const Driver* driver(size_t car) const;
    const Params& params(size_t car) const;
    const Incidents& incidents(size_t car) const;
    int line(size_t car) const;        // the line driven
    int recovery(size_t car) const;    // 0 racing, 1 backing out, 2 turning round
    // A driver's plan and outputs this step, for checks and tools.
    struct Snapshot {
        int line = -1, next = 0, mode = 0, status = 0, state = 0, follow = -1;
        float v_cur = 0, v_next = 0, steer = 0, throttle = 0, brake = 0, contact = 0, lock = 0, steer_cmd = 0;
        float line_offset = 0; // the car's distance from its line, + to the right
        float wanted_throttle = 0; // before the arcs' slew
        bool reverse = false;
        float look = 0;
        float brake_dist = 0, brake_to = 0; // the last braking check: the distance needed and the distance to the point
    };
    Snapshot snapshot(size_t car) const;
    // The rubber band's time scale for a car (1: none): what the race sets the car's step length by each step (the record's
    // time scale, Session::set_time_scale_raw with time_scale_bits). Every car's, driven or not.
    float time_scale(size_t car) const;
    u32 time_scale_bits(size_t car) const;
    // Every car's time scale into the session (call after drive(), before the session's step).
    void apply_time_scales(Session& session) const {
        for (size_t i = 0; i < session.cars().size(); ++i) session.set_time_scale_raw(i, time_scale_bits(i));
    }
    // Every car's shift byte (the driver's D +0x6F, the car's +0x83) into the session's gearbox, which applies it as 0x3F1E30
    // for control source 1 (the up-shifts with kind 0 only); 128 for a car not driven.
    void apply_shift_factors(Session& session) const;
    // The places' held starts (Pace::start_delay, seconds) into the session (60 x them as its cars' own start holds, the
    // conductor's +0xCBFC; none in a rolling start): call once before the first step.
    void apply_start_delays(Session& session);
    // The band's values as the conductor holds them (+0x7C .. +0x94, for checks against another race) and its mode.
    struct Band {
        gt3r::ee::F slow, slow_start, slow_full, fast, fast_start, fast_full, global{1.0f};
        int mode = 0;
        bool on = false;
    };
    void set_band(const Band& b) { band_ = b; }
    const Band& band() const { return band_; }

    // For checks against another race's drivers: a driver's plan and traffic state set as that race's driver object
    // holds them, and the plan's routines run on it.
    struct Probe {
        int line = 6, next = 0, mode = 0, next_kind = 0, status = 0, follow = -1, chain = 0;
        gt3r::ee::F v_cur, v_next;
        gt3r::ee::F contact;
        float s = 0;
        u16 rand = 0;
        gt3r::ee::F corner_mult{1.0f}, brake_mult{1.0f}, alongside_mult{1.0f};
        std::array<u8, 8> zone{};
        s16 cooldown = 0;
        // The rest of the driver object, for the decision (0x465078).
        int state = 0, prev_line = 6, state_hold = 0, start_flag = 0, stop_count = 0, pit = 0;
        u16 arc_ticks = 0;
        gt3r::ee::F blend, offset, gap_left{1000.0f}, gap_right{1000.0f};
        gt3r::ee::F steer, throttle, brake; // +0x30 / +0x34 / +0x38 (the outputs)
        bool reverse = false;
        u16 shift_lock = 0;
        gt3r::ee::F aim_x, aim_y; // the aim (F +0x61C / +0x620), after a decision
        int kind = 0;             // F +0x467 the driver's kind
        std::array<gt3r::ee::F, 5> pd_last{}; // D +0x00 .. +0x10 (the PD steering's last terms)
        gt3r::ee::F pd_rate;      // F +0x484
        std::array<u8, 0x40> record{}; // car +0x8C (B-spec)
        int car_class = 0;
        int threat = -1;                                        // D +0x53
        gt3r::ee::F threat_gap{1000.0f}, threat_rate{-1000.0f}; // D +0x60 / +0x64
    };
    void probe_set(size_t car, const Probe& p);
    // For checks of a steering mode this program does not use: T[2] set (the PD steering at 2 or more).
    void set_steer_mode(u8 mode) { tables_.tuning.steer_mode = mode; }
    Probe probe_get(size_t car) const;
    // 0x4674F0: the speed of line point i of the car's line.
    gt3r::ee::F probe_corner_speed(size_t car, int i);
    // 0x463EA8: the point bookkeeping and the braking; the throttle multiplier (the state changes as the routine's).
    gt3r::ee::F probe_braking(size_t car);
    // 0x465078: the decision on the state set (the cars' per-car values read again), its outputs and state after.
    Probe probe_decide(size_t car);
    // 0x3F4638: the proximity pass on the cars as they are (each car's follow set first by probe_set).
    void probe_proximity();
    // 0x46B480: line k's place at course distance s from point `hint`.
    bool probe_place(int k, int hint, gt3r::ee::F s, gt3r::ee::F& x, gt3r::ee::F& y, gt3r::ee::F& h) const { return place(k, hint, s, x, y, h); }

private:
    void proximity();
    void rubber_band();
    void decide(Driver& d);
    gt3r::ee::F braking(Driver& d);
    void bookkeeping_shift(Driver& d);
    void replan(Driver& d);
    gt3r::ee::F corner_speed(const Driver& d, int i);
    gt3r::ee::F ai_grip(const Driver& d, int surface) const;
    void speed_control(Driver& d, gt3r::ee::F target, gt3r::ee::F gain);
    void steer_to(Driver& d, gt3r::ee::F target);
    void bspec_experience(Driver& d, int table, gt3r::ee::F v);
    void pd_steer(Driver& d, gt3r::ee::F dir, gt3r::ee::F rd, gt3r::ee::F r, gt3r::ee::F amax, gt3r::ee::F ky);
    void set_kind(Driver& d, int kind);
    void stop(Driver& d, gt3r::ee::F steer);
    gt3r::ee::F hold_speed(const Driver& d, gt3r::ee::F target) const;
    void traffic(Driver& d);
    bool request_line(Driver& d, int line, bool at_once);
    // 0x46B480: line k's place at course distance s (any lap), from the span around point `hint`; false (the outputs
    // kept) where the line has none.
    bool place(int k, int hint, gt3r::ee::F s, gt3r::ee::F& x, gt3r::ee::F& y, gt3r::ee::F& h) const;
    // 0x464868: the aim on a line `look` metres on, at least 5 m from the car, relative to it; the look ahead used.
    gt3r::ee::F target(const Driver& d, int line, gt3r::ee::F look, gt3r::ee::F& dx, gt3r::ee::F& dy) const;
    // 0x4649F0: the line's heading at course distance s.
    gt3r::ee::F line_heading(const Driver& d, int line, gt3r::ee::F s) const;
    gt3r::ee::F course_s(size_t car) const; // the car's course distance as the race has it (F +0x4A4)
    void incidents_after(Driver& d);
    struct Near;
    void zones(Near& z, gt3r::ee::F dist, gt3r::ee::F u, gt3r::ee::F v, gt3r::ee::F ahead, gt3r::ee::F behind, gt3r::ee::F w) const;
    bool closing(size_t a, size_t b, gt3r::ee::F v, gt3r::ee::F& gap, gt3r::ee::F& rate) const;
    void contact(size_t a, size_t b, gt3r::ee::F gap, gt3r::ee::F rate);

    void init(const course::AiData& data, const Program& program, std::span<const size_t> computer);
    void setup(Driver& d);
    std::array<std::optional<std::array<u8, 0x80>>, 4> sets_; // the course's driver sets by driveline class

    std::unique_ptr<SessionCars> own_cars_;
    const Cars* session_;
    Tables tables_;
    CourseLines lines_;
    Pace pace_;
    std::vector<std::unique_ptr<Driver>> drivers_; // by car index (null: not driven)
    std::vector<int> ranks_;
    u64 steps_ = 0;
    u32 clock_ = 0; // the race's clock as the conductor keeps it (+0xF89C): 50 a step, a third of it in milliseconds
    // Per car (every car, driven or not): the proximity pass's results (the driver object's +0x44 .. +0x6A).
    std::vector<Near> near_;
    // Per car: the lateral acceleration's filter (F +0x55C / +0x560) and the speed along the course (F +0x650) where the
    // cars do not give them, and the last course distance.
    std::vector<gt3r::ee::F> lat_v_, lat_x_, rate_, last_s_;
    std::vector<gt3r::ee::F> scale_; // each car's time scale (F +0x444)
    Band band_;
    u8 limit_player_ = 0, limit_computer_ = 0;
    std::array<u32, 24> soft_{}; // a material's soft flag (the program's table)
};

} // namespace gt4r::sim::ai
