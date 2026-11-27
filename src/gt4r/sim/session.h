#pragma once

#include <optional>

// A session's cars stepped on a course: race::Simulation over the shared engine's vehicle step with
// Gran Turismo 4's cars, tables and road (docs/specs/physics.md "What runs where"). One step is 1/60 s.

#include "gt4r/race/simulation.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/collide.h"
#include "gt4r/sim/aids_prologue.h"
#include "gt4r/sim/collide_prologue.h"
#include "gt4r/sim/feel_prologue.h"
#include "gt4r/sim/phases_prologue.h"
#include "gt4r/sim/convert.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_prologue.h"
#include "gt4r/sim/rules_step.h"
#include "sim/exact/course_query.h"
#include "sim/exact/tick.h"

#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace gt4r {
class Disc;
}

namespace gt4r::sim {

// A car of the session.
struct Entry {
    Equipment equipment;   // the car, its parts and settings (stock_car)
    bool automatic = true; // the gearbox shifts itself
    int pedal_style = -1;  // the entry's own pedal style byte in its place (Prologue's recorded runs); -1: by `automatic`
    bool asm_on = true, tcs_on = true;
    bool wheel_aid = false; // the entry's third aid switch (+0x3458; off for a player in the races seen): a steering wheel's slip aid
    // The entry's condition block (+0x3468, 0x20 bytes: the odometer (u32, m), the engine's and the oil's distances, the dirt,
    // the body's distance (floats), +0x14 no wear): a garage car's own; none: the program's default, all 0 with no wear (0x3433E8).
    std::optional<std::array<u8, 0x20>> condition;
    int grid_place = -1;   // the course's start place (-1: the entry's index)
    float start_speed = 0; // a rolling start: metres per second along the place's heading at time 0
    // A rolling start's place: metres along the course from the start line (negative: before it), the car on
    // the road's middle there, facing along the course (NaN: the grid place). The race's rule for it:
    // rules::rolling_start_distance (rules_start.h).
    float start_distance = std::numeric_limits<float>::quiet_NaN();
    // The car's own start place, in place of its grid place (a license test's road section has its own: 0x3952C0); a plain
    // grid start (start kind 0).
    std::optional<course::Runway::Place> place;
    // The start's kind for a car on its grid place (or its own place): 0 a grid start; 6 a pit start (the free practice's player,
    // DynamicsConductorFreePractice::vf30 0x3F4F10: held two seconds, rules_start.cpp); 8 as 0x363B10 has it.
    int grid_kind = 0;
    // The car's start on a driving line (the set-up's start kinds 1 and 2: race::car_start, CourseLines::start_place), in place
    // of its grid place and of start_distance: the place and heading in the record's frame (x, y, z up), the speed (km/h,
    // whole; 0 standing), F + 0x65B.
    struct LineStart {
        int kind = 1;
        gt3r::ee::F x, y, z, heading;
        s32 hint = -1; // the road triangle there
        int speed = 0;
        u8 mark = 0;
        // Whether a moving start opens the throttle (0x463B98: the car's driver is on a straight of its line) at the car's
        // distance along the course; none: open.
        std::function<bool(gt3r::ee::F)> open_throttle;
    };
    std::optional<LineStart> line_start;
    // Whether a moving start from the grid (start_speed, start kind 0) opens the throttle, as LineStart::open_throttle; none: open.
    std::function<bool(gt3r::ee::F)> open_throttle;
    u8 driver = 0;     // the entry's +0x344C: 0 a pad, 1 and 2 the steering wheels (F + 0x547 2 and 3), any other a computer's
    gt3r::s8 control = 0; // F + 0x466 at the set-up: 0 the player's pad, 1 B-spec, 2 the computer
    // The race uses fuel (its rule block's byte +0x4C): the tank filled at the start (the car's tank less the program's byte
    // 0x620E24), burnt by the engine's output, its weight in the mass. Off: no tank (the races seen).
    bool fuel = false;
};

// What a race takes of its course's COURSE row: +0x10 the kind (0 dry, 1 wet, 2 dirt, 3 snow; 0xFF reads 0) and +0x12 (2 on
// the gymkhana and special stages: no active steering, 0x347A00).
struct CourseRow {
    u8 kind = 0, road = 0;
};
// By the course's label ("tsukuba"); none without a row.
std::optional<CourseRow> course_row(db::SpecDb& db, const std::string& course);

class Session final : public race::Simulation {
public:
    // A computer driver's decision for a car, within the step (step_once).
    using Decider = std::function<void(size_t car, race::CarInput& input)>;
    static constexpr float kStep = 1.0f / 60.0f;
    inline static bool skip_hold = false; // (gt4r_diff's comparison of the set-up: the constructor without its hold)
    static constexpr int kHoldSteps = 120; // a standing start's countdown before the session's time 0 (the constructor's)
    // Further settings of a session.
    struct Options {
        int hold_steps = kHoldSteps; // the constructor's countdown (0: the caller's own hold, from the set-up's state)
        std::array<u8, 0x80> rules{}; // the race's rule block (race::event_rule_block: the computer cars' axle grips)
    };

    // `course`: the course's row; without it the kind is guessed (dirt when the first start place is on a loose surface).
    // Throws std::runtime_error for a session that cannot be set up (no entries, a course without a
    // start place on the road, a car whose parameters cannot be built).
    Session(db::SpecDb& db, const Program& program, std::shared_ptr<const course::Runway> road, std::span<const Entry> entries,
            std::optional<CourseRow> course = {}, const Options* options = nullptr);
    ~Session() override;
    // A session on a disc's course ("tsukuba": the file crs/tsukuba) with cars as sold, by their labels
    // ("nsx_r_02"), in grid order. Throws std::runtime_error (no such course or car, or as the constructor).
    static std::unique_ptr<Session> open(Disc& disc, const std::string& course, std::span<const std::string> cars);

    // Advances by dt in whole steps of 1/60 s (the remainder is kept for the next call).
    void step(std::span<const race::CarInput> inputs, float dt) override;
    std::span<const race::CarState> cars() const override { return states_; }

    // One step of 1/60 s. `decide`: each car's computer driver, asked for its input where the program's drivers decide (0x465078
    // in the input phase 0x349FD0, after the preparation 0x357700: the step's own slip angles, peak forces and torques); the
    // inputs given then only say who drives (computer, control).
    // `near_pass`: the drivers' proximity pass where the program's runs (0x3F4638, phase 14: after the collision step, before the
    // bodies' phases and the distance along the course).
    void step_once(std::span<const race::CarInput> inputs, const Decider& decide = {}, const std::function<void()>& near_pass = {});
    // One step of the race's countdown: every car held (F + 0x4BC, 0x34C210; the engines answer the throttle, the
    // cars stay); the session's time does not advance.
    void hold_once(std::span<const race::CarInput> inputs, const Decider& decide = {}, const std::function<void()>& near_pass = {});
    // The steps of its hold a car has left (0x34C1C8: the race's countdown and its own): its own start's hold here.
    u32 start_counter(size_t car) const { return car < start_counters_.size() ? start_counters_[car] : 0; }
    // The race's countdown (the race information's +0xBC): `steps` steps from the next, counted down one a step, the
    // cars held and the session's time standing while it runs. 0 ends it.
    void set_countdown(u32 steps) { countdown_ = steps; }
    // The race's class grips (the rule block's +0x18 .. +0x1B, conductor + 0xF830: a computer-driven car's extra grip per
    // drive class, percent). The session starts with a single race's (0x353570: 20 for each the event leaves 0); the
    // other conductors (a license test's, ...) keep the event's bytes as they are.
    void set_class_grip(const std::array<gt3r::s8, 4>& grip) { tables_.class_grip = grip, race_.class_grip = grip; } // (Prologue's step reads race_'s)
    u32 countdown() const { return countdown_; }
    // A car's crossings of the line as its race counts them (F + 0x4AC: the rolling start's pace and the pad's lap read it).
    void set_crossings(size_t car, int n);
    int crossings(size_t car) const; // as set, else by the session's own laps
    // A car's place in the race (F + 0x4AF, 1 the leader).
    void set_rank(size_t car, int place);
    // A car held in its rolling start (F + 0x467 = 1: its computer driver drives it, the rolling start's pace caps it,
    // the input phase takes the car's input as the computer's outputs) let go (the single-player conductor's virtual
    // +0x14, 0x347928: 0x345228 and 0x3F37B8).
    void release_start(int car) override;
    // A free run's count to the hand-over (F +0x590: 180 steps from the set-up; 0x3F37C0 in DynamicsConductorFreeRun::vf14), a step
    // of it: true in the step it runs out.
    bool count_rolling(size_t car);
    // The car standing at (x, y, z) facing `heading`, in the record's frame (x, y, z up: the record's F + 0x00; the
    // course's x, -z, y; z the road's height there): the pose (0x3630C0), 0x3455F0 and the standing start (0x363560).
    // For a pit stop; Gran Turismo 4's rules only.
    void place_standing(size_t car, gt3r::ee::F x, gt3r::ee::F y, gt3r::ee::F z, gt3r::ee::F heading);
    // A car stopped in its pit box (0x363560, the standing state where it is; the hold turns it to `heading`, 0x36AE20), and turned
    // there at once (the pass at the stop's second step: F +0x570 and the axes).
    void stop_in_box(size_t car, gt3r::ee::F heading);
    void face(size_t car, gt3r::ee::F heading);
    // The car's hold counter (F + 0x4BA) as its caller's pit stop keeps it (the computer drivers' PitStops runs the stop's
    // timing, 0x35C0B8, on its own record): the car is held (the attitude's start hold 0x36AE20, the input's), and once a
    // car's counter is given so the session's preparation no longer runs 0x35C0B8 for it. Set it each step.
    void set_pit_hold(size_t car, u16 steps);
    // The hold counter as the set-up left it (a pit start's 120, rules_start.cpp), for the caller that keeps it from then on.
    u16 start_hold_of(size_t car) const { return car < cars_.size() ? u16(cars_[car].hold) : u16(0); }
    // A car's own start hold (conductor + 0xCBFC + 4 x car, set at the race's set-up from the event's seconds for its place:
    // 60 x them, the missions' "catch the cars ahead"): the car held (F + 0x4BC) that many steps after the start's count
    // (0x34DB30, rules_start's start_hold). 0: none.
    void set_start_delay(size_t car, u32 steps);
    double time() const { return double(steps_) * double(kStep); }
    const RoadGround& ground() const { return *ground_; }
    // A car's record in the vehicle step (the engine's mirror of the car record).
    const gt3r::sim::exact::Car& record(size_t i) const { return cars_[i]; }
    // Each car's spec as built for this course.
    const Spec& spec(size_t i) const { return specs_[i]; }
    // A car's time scale (1: normal; its step is time_scale / 60 s): what a rubber band sets per car
    // (the record's F +0x524). Set it before a step; it holds until set again. As raw EE float bits,
    // so a caller's EE arithmetic carries through unchanged.
    void set_time_scale_raw(size_t i, u32 bits) { if (i < cars_.size()) cars_[i].time_scale = gt3r::ee::F::raw(bits); }
    // The course is a loose one (its first start place is on a loose surface).
    bool loose() const { return loose_; }
    // The course's kind as the race has it (the COURSE row's +0x10 through 0x34C968, conductor + 0xCBDD: 0 a road course,
    // 2 dirt, 3 snow): the input phase's course byte, the sliding effects'; the aids and the road bumps' tables go by its
    // category (0x34C998: dirt 1, snow 2; the bump tables' surface 5 takes row 15 on snow). The session starts with 2 when its first start place is on a loose surface, else 0.
    void set_course_kind(const Program& program, u8 kind);
    u8 course_kind() const { return course_kind_; }
    // Gran Turismo 4's own routines where they are ported (rules_step.h; the default), or the shared
    // engine's throughout (Gran Turismo 3's rules on Gran Turismo 4's cars), for comparison.
    void set_gt4_rules(bool on) { gt4_rules_ = on && gt4_ready_; }
    // SCPS-17001 v1.05's program: its own rules where they differ from v1.01's.
    bool japan() const { return japan_; }
    bool gt4_rules() const { return gt4_rules_; }
    // Whether the session runs Gran Turismo 4 Prologue's own step (its program recognized; rules_prologue.h).
    bool prologue_rules() const { return prologue_.has_value(); }
    // The driver's kind F +0x467 (Prologue's +0x538) as its driver has it after the car's run (ai::Field::kind; 8 parked).
    void set_ended_kind(size_t i, u8 kind) { cars_[i].driver_kind = kind; }
    // A car's Gran Turismo 4 part of the record (record.h) and wheel layout.
    const sim::Extra& gt4_record(size_t i) const { return gx_[i]; }
    // The steering wheel's force of the last step (the pad block's +0x02: 0x80 centered, 0x80 - the force (-1 .. 1) x 127;
    // rules::steer_force, or the engine's on its own rules).
    u8 steer_force(size_t i) const { return cars_[i].steer_force; }
    // A car's controller's own wheel options (RaceInput::vf4: the Driving Force Pro's POWER 0 .. 2, the strength's index, and its
    // POWER_ASSIST, the steering change's factor on; 1 and 1 without them). They hold until set again.
    void set_wheel_options(size_t car, gt3r::s16 strength, gt3r::s16 turn_factor) {
        if (car >= wheel_options_.size()) wheel_options_.resize(car + 1, {gt3r::s16(1), gt3r::s16(1)});
        wheel_options_[car] = {std::clamp<gt3r::s16>(strength, 0, 2), turn_factor};
    }
    // The hits of the last step in the program's order (0x350530: the car, 0 a wall, 1 a car, 2 a pylon, the part, the strength).
    const std::vector<collide::Hit>& hits() const { return hits_; }
    // The aid block of the computer driver that drives a car (rules::driver_block from the course's set for the car:
    // 0x466C20), which Gran Turismo 4's aids use while a computer drives it. Without one a computer's car has no aids.
    void set_driver_aids(size_t car, const gt3r::sim::exact::Aids& block);
    // And its grip by tire class (the parameter block's +0xA8: 0x467470), which scales the car's tires' peak forces.
    void set_driver_aids(size_t car, const gt3r::sim::exact::Aids& block, const std::array<gt3r::ee::F, 3>& grip);
    // The driving line's heading at a car (0x4649F0 on its driver's line at its distance along the course: the computer
    // drivers' Field::line_heading_at): the active steering of a pad car aims along it. Set it before each step; a car never
    // given one runs without the active steering.
    // `mode`: the driver's (D +0x1E, Field::line_mode_at: the line point's kind, 3 / 4 at a braking point), kept in the
    // record: the active steering runs in mode 0 only, on a straight.
    void set_line_heading(size_t car, gt3r::ee::F heading, std::optional<u8> mode = {});
    // For gt4r_diff's whole-step comparison: a car's state set as a whole (the engine's mirror and Gran Turismo 4's
    // part), and its collision body.
    void load_state(size_t i, const gt3r::sim::exact::Car& c, const sim::Extra& x) { cars_[i] = c, gx_[i] = x; }
    // A car put at a moment of a recorded race (a free run's recording: its record at the line); its laps from 0.
    void load_moment(size_t i, const gt3r::sim::exact::Car& c, const sim::Extra& x);
    // And Prologue's part of a car (rules_prologue.h; a session running Prologue's step).
    const prologue::CarPart& prologue_part(size_t i) const { return prologue_cars_[i]; }
    void load_prologue_part(size_t i, const prologue::CarPart& x) { prologue_cars_[i] = x; }
    // Prologue's collision step (collide_prologue.h; null on Gran Turismo 4's disc).
    const prologue::Collision* prologue_collision() const { return prologue_collide_.get(); }
    collide::Body& body(size_t i) { return bodies_[i]; }
    const collide::Body& body(size_t i) const { return bodies_[i]; }
    const rules::WheelLayout& wheel_layout(size_t i) const { return layouts_[i]; }
    // The wheels' emits of the last step (0x3668F0 in phase 19: the smoke, dust and spray the program hands the course's
    // effects), car by car in the step's order.
    const std::vector<rules::TireEmit>& tire_emits() const { return emits_; }
    // What the session runs on stand-ins for ("the disc's program is not a build whose tables are mapped").
    const std::vector<std::string>& notes() const { return notes_; }

    // The race's wear rules (the conductor + 4, 0x367C90) from its rule block (0x80 bytes: the tread wear's curve at +0x2C ..
    // +0x34) and whether the second wear may run (a single race on a paved course whose row allows it). Set before the start
    // (new tires take them); all off by default, as in every race seen.
    void set_wear_rules(const u8* rule_block, bool second_on, const Program& program);
    // The race's rules of hits and speed caps (the rule block's +0x46 .. +0x4A, +0x42 / +0x43 and +0x4B, from the race's RACE row:
    // Gran Turismo 4's 0x35E108, Prologue's 0x3C5978): a hit past the limits caps the car's speed at `penalty_cap` km/h for
    // `h.penalty` seconds. All 0 (none) by default; holds until set again.
    void set_race_rules(const rules::HitRules& h, std::array<u8, 2> speed_cap, u8 penalty_cap) {
        hit_rules_ = h, aid_race_.speed_cap = speed_cap, aid_race_.start_cap = penalty_cap;
    }
    // The seconds left of a car's penalty as the race display counts them (Prologue's 0x3C6030, Gran Turismo 4's the same: the counter's
    // steps + 59 over 60; 0 once the car's race ended).
    int penalty_seconds(size_t i) const;
    // Whether a car is held to a rolling start's pace (0x3F7880 answering 1), which the opening shots ask.
    bool held_to_pace(size_t i) const;
    // What the computer drivers' pit flow reads and changes (sim/ai/pit_flow.h): the fuel (liters) and tank, the distance and the
    // fuel since the economy display's last reading (F +0x4C8 / +0x4C4), the fuel counted at all (F +0x5D0's distance > 0 is the
    // flow's own test), a wheel's tread wear (+0xAC) and wear grip (+0x50); new tires on a wheel (0x362E58); the fuel set.
    // A car's condition block as the race has left it (F + 0x5D8).
    std::array<u8, 0x20> condition(size_t i) const;
    float fuel(size_t i) const { return gx_[i].drive.fuel.fuel.host(); }
    float tank(size_t i) const { return gx_[i].drive.fuel.capacity.host(); }
    float fuel_distance(size_t i) const { return gx_[i].drive.fuel.economy_distance.host(); }
    float fuel_used(size_t i) const { return gx_[i].drive.fuel.economy_used.host(); }
    void set_fuel(size_t i, gt3r::ee::F liters);
    float tread(size_t i, int wheel) const { return cars_[i].w[size_t(wheel)].wear.host(); }
    float wear_grip(size_t i, int wheel) const { return cars_[i].w[size_t(wheel)].wear_grip.host(); }
    const rules::WearRules& wear_rules() const { return tables_.wear; }
    // A wheel's wear level as the race display takes it (wheel +0xB1, -0x80 .. 0x7F).
    int wear_level(size_t i, int wheel) const { return i < cars_.size() ? int(cars_[i].w[size_t(wheel)].wear_level) : -0x80; }
    void new_tire(size_t i, int wheel);
    // A pit stop's tire change (0x35C0B8, sim/pit_tires.h): at the stop's 645th step from its end the converter's tire part
    // (0x361570) on the car's parameter block from `spec` (the entry's equipment with the chosen tires, its spec for this
    // course) for `axles` (1 front, 2 rear, 3 both), at the 644th the records' base grips (0x361798); the car's tire records
    // in its record read again from the block. Gran Turismo 4's rules only.
    void change_tires(size_t car, const Spec& spec, const Program& program, u32 axles);
    void retune_tires(size_t car, const Program& program, u32 axles);
    // The entry's equipment, and the car's parameter block as its set-up built it and the tire changes since left it.
    const Equipment& equipment(size_t i) const { return equipment_[i]; }
    const ParamBlock& params(size_t i) const { return blocks_[i]; }
    // The flaps' random stream (the program's 0x874D50), for gt4r_diff's whole step.
    void set_random_state(u32 state) { random_state_ = state; }
    // Prologue's (the VU0's R), likewise.
    void set_vu0_random(u32 r) { rng_.r = r; }
    // A car's shift byte (car +0x83; 128: none): its gearbox's shift speeds times it / 128 while a B-spec driver drives it
    // (control source 1, 0x3F1E30). Holds until set again.
    // The car's rating, the parameter block's +0xFE0 (0x3F86E8: 0.01 .. 1), which the B-spec skill classes cars by.
    float rating(size_t i) const { return gx_[i].drive.rating.host(); }
    // 0x36BF68: the gear the gearbox picks for a speed (m/s) on car i (the race display's suggested gear): 0 backwards, 1 for a
    // one-speed or stepless box of kind 1, else the lowest whose up-shift speed (less 2,000 rpm in first, 500 after; a B-spec
    // driver's factor in it) the speed is under, else the top gear.
    int gear_for_speed(size_t i, gt3r::ee::F speed) const;
    // What the drivers and the pit pass keep of a car in the program's record: the driver's kind (F +0x467; the rolling start's 1 is
    // the session's own) and the pit flags (F +0x4B1; its bit 0x10 the session's).
    void set_driver_kind(size_t car, u8 kind) {
        if (car < cars_.size() && gt4_rules_ && kind != 1 && cars_[car].driver_kind != 1) cars_[car].driver_kind = decltype(cars_[car].driver_kind)(kind);
    }
    void set_finished(size_t car) { // (F +0x4B2)
        if (car < cars_.size() && gt4_rules_) cars_[car].finished = 1, gx_[car].phases.finished = 1;
    }
    // F +0x62E, one byte the hits' penalty and the pit flow share: the arcade's "Passing pit lane" penalty sets 1 (0x35FE18,
    // the rule block's +0x4A = 0xFF), the pit flow takes it as a pad car's pit visit asked (2 on the lane, 0 out of its end:
    // 0x35B060), and a new penalty waits while it is not 0.
    u8 pit_mark(size_t car) const { return car < gx_.size() ? gx_[car].phases.penalty_marked : 0; }
    void set_pit_mark(size_t car, u8 v) {
        if (car < gx_.size()) gx_[car].phases.penalty_marked = v;
    }
    void set_pit_flags(size_t car, u8 flags) {
        if (car < cars_.size() && gt4_rules_) cars_[car].x535 = u8((cars_[car].x535 & 0x10) | (flags & ~0x10));
    }
    // A driver's pace level (car +0x81, D +0x6D: 0 .. 4; 2 as the set-up leaves it): the TCS target's factor of a B-spec driver's car.
    void set_pace_level(size_t car, int level) {
        if (car < gx_.size()) gx_[car].aids.tcs_class = u8(level);
    }
    void set_shift_factor(size_t i, u8 factor) {
        if (i < gx_.size()) gx_[i].drive.shift_factor = factor;
    }

    // ---- online races (docs/specs/net.md "Race"): cars their owners' machines run ----
    // A posed car is not stepped here: its caller gives its record at the end of the coming step before each step (pose). It is
    // taken in between phases 12 and 13 (so the drivers decide while it, as every car, has not moved yet); the step skips its
    // phases 1 .. 12 and 15 .. 22 but keeps it in the collision step, whose result for it is thrown away (its
    // body moves along the posed motion), and in phase 14 (the others' slipstream).
    void set_posed(size_t car, bool on);
    bool posed(size_t car) const { return car < posed_.size() && posed_[car] != 0; }
    // Car `car`'s record from car `other` of another session (its prediction, run on one car alone): all of it but what the
    // race here keeps (the index, the laps, the place, the race's end, the ghost flag, who is near it).
    void pose(size_t car, const Session& from, size_t other = 0);
    // What a posed car is shown displaced by (the correction blended out): metres, and radians about the course's up axis.
    struct Shown {
        Vec3 shift{};
        float yaw = 0;
    };
    void set_shown(size_t car, const Shown& s);
    // A car not in the race for the collision step, the slipstream and the drivers (F + 0x441).
    void set_ghost(size_t car, bool on);
    // A car alone on this course as it stands now: its record, its body and the race's tables (a prediction runs on from it).
    std::unique_ptr<Session> solo(size_t car) const;
    // What phase 14 gave a car as its slipstream (F + 0x59C), and to give a car: a prediction runs alone, its race knows who is
    // ahead of it. And the driving line's heading and mode as the car's caller gave them.
    gt3r::ee::F slipstream(size_t car) const { return car < cars_.size() ? cars_[car].slip_target : gt3r::ee::F(); }
    void set_slipstream(size_t car, gt3r::ee::F x) {
        if (car < cars_.size()) cars_[car].slip_target = x;
    }
    std::optional<gt3r::ee::F> line_heading(size_t car) const { return car < line_heading_.size() ? line_heading_[car] : std::nullopt; }
    u8 line_mode(size_t car) const { return car < gx_.size() ? gx_[car].input.driver_32 : 0; }
    // All the step keeps of a car, to run on from again.
    struct Memory;
    void save(size_t car, Memory& m) const;
    void restore(size_t car, const Memory& m);
    // The last step's hits between cars: the impulse (N s, the course's frame) `struck` took from `striker` at the point (x, z).
    struct CarContact {
        int struck = 0, striker = 0;
        float x = 0, z = 0, jx = 0, jz = 0;
    };
    const std::vector<CarContact>& car_contacts() const { return car_contacts_; }
    // An impulse on a car as the collision step's car response gives one: a hit another machine saw and this one did not.
    void apply_impulse(size_t car, float x, float z, float jx, float jz);
    // The road's triangle under a moved car's hint (c.road_seg) and its axes and hubs from its attitude.
    void settle_moved(size_t car, gt3r::sim::exact::Vec3 was);

private:
    Session() = default; // (solo)
    struct Extra { // per car, outside the record
        int checkpoint = -1; // distance_along's hint
        float last_v = 0;
        bool has_v = false;
        u8 kinds[4] = {};    // each wheel's road triangle flags
        u8 materials[4] = {}; // and its material
        u8 prev_buttons = 0;
        float spin[4] = {};       // each wheel's angle rolled
        float rest_drop[4] = {};  // each wheel's center below its suspension point, at rest
        bool rest_set[4] = {};
        bool airborne[4] = {};    // each wheel without load at the last step's end
        // This step's contact from the collision step (race::CarState::contact ..).
        float contact = 0;
        u8 contact_kind = 0;
        int contact_car = -1;
        float wall_hit = 0, car_hit = 0; // (race::CarState::wall_hit, car_hit)
    };
    void place(size_t i, const course::Runway::Place& at, bool by_front = true, s32 hint = -1, const gt3r::ee::F* line_heading = nullptr);
    void setup(size_t i, const course::Runway::Place& at, int kind, float speed, bool fuel, const Entry::LineStart* line, gt3r::s8 control, u8 driver,
               const std::function<bool(gt3r::ee::F)>& open_throttle);
    rules::PoseHooks pose_hooks();
    void ground_contact(gt3r::sim::exact::Car& c, Extra& x);
    void chassis(size_t i);
    void collide();
    void judge_hits();
    void publish(size_t i);

    std::shared_ptr<const course::Runway> road_;
    std::shared_ptr<RoadGround> ground_;
    std::unique_ptr<gt3r::sim::exact::CourseGround> course_ground_; // (Prologue's road: the program's query on its GTRW)
    std::unique_ptr<gt3r::sim::exact::CourseQuery> course_query_;   // (and its single-point queries: the set-up's ground 0x3881F8, the progress 0x388198)
    std::shared_ptr<gt3r::sim::exact::ProgramData> data_;
    gt3r::sim::exact::RaceInputs race_;
    std::vector<gt3r::sim::exact::Car> cars_;
    std::shared_ptr<collide::Walls> walls_;
    std::vector<collide::Body> bodies_;
    std::vector<collide::CarIn> body_cars_;
    std::vector<Extra> extra_;
    std::vector<sim::Extra> gx_;
    std::vector<rules::WheelLayout> layouts_;
    std::vector<rules::SpeedTables> speed_tables_; // (0x35DE20's, per car: its special row)
    std::vector<rules::TireEmit> emits_;           // the last step's wheels' emits (0x3668F0)
    std::vector<std::optional<gt3r::sim::exact::Aids>> driver_aids_; // a computer driver's aid block per car
    std::vector<std::optional<std::array<gt3r::ee::F, 3>>> driver_grip_; // and its grip by tire class
    rules::AidRace aid_race_;
    rules::NearPairs pairs_; // phase 14's tables of the cars' pairs
    rules::MoodTables mood_tables_;
    rules::HitRules hit_rules_;             // (the rule block's +0x46 .. +0x4A: all 0 in a single race)
    s16 pylons_hit_ = 0;                    // cond + 0x1003C
    std::vector<rules::PadMail> mail_;      // each car's request to its controller (a pit stop's answer, a B-spec command)
    std::vector<std::array<gt3r::s16, 2>> wheel_options_; // set_wheel_options
    std::vector<collide::Hit> hits_;        // the collision step's hits this step
    u32 active_on_ = 0;                     // the active steering's switch as the controls' tables have it (0x84357C)
    std::vector<std::optional<gt3r::ee::F>> hold_heading_; // a car stood in its pit box: the box's heading (the hold's, 0x46BE70)
    std::vector<std::optional<gt3r::ee::F>> line_heading_; // each car's driving line's heading, when its caller gives it
    std::vector<u8> pit_by_caller_;         // the car's hold counter is its caller's (set_pit_hold)
    rules::Tables tables_;
    rules::StartTables start_tables_;
    std::vector<u32> start_counters_; // each car's own start hold counter (conductor + 0xCBFC + 4 i: 0 in a single race)
    std::vector<u8> pace_kmh_;        // each car's rolling start's pace (0x3F7880: the entry's speed, km/h)
    u32 countdown_ = 0;               // the race's countdown (the race information's +0xBC)
    bool holding_ = false;            // (hold_once's step)
    bool gt4_ready_ = false, gt4_rules_ = false;
    bool japan_ = false; // SCPS-17001 v1.05's program: its own rules where they differ (docs/specs/physics.md "Japan's rules")
    // Prologue's program: its own step (rules_prologue.h) in place of the engine's routines it does not share.
    std::optional<prologue::Tables> prologue_;
    std::vector<prologue::CarPart> prologue_cars_;
    std::vector<prologue::FeelPart> prologue_feel_; // (phase 19's bookkeeping and feel, feel_prologue.h)
    rules::FeelTables prologue_feel_tables_;
    rules::SlideTables prologue_slide_;
    prologue::ForceTables prologue_force_; // (0x3BC7F0's tables)
    gt3r::sim::exact::Proximity prologue_near_{}; // (phase 14's tables of the cars' pairs)
    std::unique_ptr<prologue::Collision> prologue_collide_;
    prologue::AidTables prologue_aids_;
    prologue::StepTables prologue_step_; // (phases 4 and 22's tables: phases_prologue.h)
    std::vector<prologue::Collision::Contact> prologue_contacts_;
    std::vector<Spec> specs_;
    std::vector<Equipment> equipment_; // (each entry's)
    std::vector<ParamBlock> blocks_;   // each car's parameter block (Gran Turismo 4's rules)
    void reload_tires(size_t car, const Program& program);
    std::vector<race::CarState> states_;
    std::vector<std::string> notes_;
    gt3r::Vu0Random rng_;
    bool crossings_set_ = false;
    u32 random_state_ = 0; // (the program's shared random stream, 0x874D50, for the flaps: ours from 0)
    bool loose_ = false;
    u8 course_kind_ = 0, course_road_ = 0;
    float pending_ = 0;
    u64 steps_ = 0;
    std::vector<u8> posed_;
    std::vector<Shown> shown_;
    std::vector<CarContact> car_contacts_;
    struct Pose { // a posed car's record for the end of the coming step
        bool set = false;
        gt3r::sim::exact::Car car;
        sim::Extra gt4;
        Extra own;
        std::vector<rules::TireEmit> emits;
    };
    std::vector<Pose> poses_;
    void take_pose(size_t car);
};

struct Session::Memory {
    gt3r::sim::exact::Car car;
    sim::Extra gt4;
    Extra own;
    collide::Body body;
    rules::PadMail mail;
    u32 random = 0, countdown = 0;
    u64 steps = 0;
    ParamBlock block; // (a pit stop's tire change rewrites the car's block: change_tires, retune_tires)
};

} // namespace gt4r::sim
