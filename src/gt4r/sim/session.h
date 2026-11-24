#pragma once

#include <optional>

// A session's cars stepped on a course: race::Simulation over the shared engine's vehicle step with
// Gran Turismo 4's cars, tables and road (docs/specs/physics.md "What runs where"). One step is 1/60 s.

#include "gt4r/race/simulation.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/collide.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_step.h"
#include "sim/exact/tick.h"

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
    bool asm_on = true, tcs_on = true;
    int grid_place = -1;   // the course's start place (-1: the entry's index)
    float start_speed = 0; // a rolling start: metres per second along the place's heading at time 0
    // A rolling start's place: metres along the course from the start line (negative: before it), the car on
    // the road's middle there, facing along the course (NaN: the grid place). The race's rule for it:
    // rules::rolling_start_distance (rules_start.h).
    float start_distance = std::numeric_limits<float>::quiet_NaN();
    // The race uses fuel (its rule block's byte +0x4C): the tank filled at the start (the car's tank less the program's byte
    // 0x620E24), burnt by the engine's output, its weight in the mass. Off: no tank (the races seen).
    bool fuel = false;
};

class Session final : public race::Simulation {
public:
    static constexpr float kStep = 1.0f / 60.0f;
    static constexpr int kHoldSteps = 120; // the hold before the session's time 0

    // Throws std::runtime_error for a session that cannot be set up (no entries, a course without a
    // start place on the road, a car whose parameters cannot be built).
    Session(db::SpecDb& db, const Program& program, std::shared_ptr<const course::Runway> road, std::span<const Entry> entries);
    ~Session() override;
    // A session on a disc's course ("tsukuba": the file crs/tsukuba) with cars as sold, by their labels
    // ("nsx_r_02"), in grid order. Throws std::runtime_error (no such course or car, or as the constructor).
    static std::unique_ptr<Session> open(Disc& disc, const std::string& course, std::span<const std::string> cars);

    // Advances by dt in whole steps of 1/60 s (the remainder is kept for the next call).
    void step(std::span<const race::CarInput> inputs, float dt) override;
    std::span<const race::CarState> cars() const override { return states_; }

    // One step of 1/60 s.
    void step_once(std::span<const race::CarInput> inputs);
    // One step with the cars held as during a start's countdown (the engines answer the throttle, the
    // cars stay); the session's time does not advance.
    void hold_once(std::span<const race::CarInput> inputs);
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
    // Gran Turismo 4's own routines where they are ported (rules_step.h; the default), or the shared
    // engine's throughout (Gran Turismo 3's rules on Gran Turismo 4's cars), for comparison.
    void set_gt4_rules(bool on) { gt4_rules_ = on && gt4_ready_; }
    bool gt4_rules() const { return gt4_rules_; }
    // A car's Gran Turismo 4 part of the record (record.h) and wheel layout.
    const sim::Extra& gt4_record(size_t i) const { return gx_[i]; }
    // The aid block of the computer driver that drives a car (rules::driver_block from the course's set for the car:
    // 0x466C20), which Gran Turismo 4's aids use while a computer drives it. Without one a computer's car has no aids.
    void set_driver_aids(size_t car, const gt3r::sim::exact::Aids& block);
    // And its grip by tire class (the parameter block's +0xA8: 0x467470), which scales the car's tires' peak forces.
    void set_driver_aids(size_t car, const gt3r::sim::exact::Aids& block, const std::array<gt3r::ee::F, 3>& grip);
    // For gt4r_diff's whole-step comparison: a car's state set as a whole (the engine's mirror and Gran Turismo 4's
    // part), and its collision body.
    void load_state(size_t i, const gt3r::sim::exact::Car& c, const sim::Extra& x) { cars_[i] = c, gx_[i] = x; }
    collide::Body& body(size_t i) { return bodies_[i]; }
    const rules::WheelLayout& wheel_layout(size_t i) const { return layouts_[i]; }
    // What the session runs on stand-ins for ("the disc's program is not a build whose tables are mapped").
    const std::vector<std::string>& notes() const { return notes_; }

    // The race's wear rules (the conductor + 4, 0x367C90) from its rule block (0x80 bytes: the tread wear's curve at +0x2C ..
    // +0x34) and whether the second wear may run (a single race on a paved course whose row allows it). Set before the start
    // (new tires take them); all off by default, as in every race seen.
    void set_wear_rules(const u8* rule_block, bool second_on, const Program& program);
    // What the computer drivers' pit flow reads and changes (sim/ai/pit_flow.h): the fuel (liters) and tank, the distance and the
    // fuel since the economy display's last reading (F +0x4C8 / +0x4C4), the fuel counted at all (F +0x5D0's distance > 0 is the
    // flow's own test), a wheel's tread wear (+0xAC) and wear grip (+0x50); new tires on a wheel (0x362E58); the fuel set.
    float fuel(size_t i) const { return gx_[i].drive.fuel.fuel.host(); }
    float tank(size_t i) const { return gx_[i].drive.fuel.capacity.host(); }
    float fuel_distance(size_t i) const { return gx_[i].drive.fuel.economy_distance.host(); }
    float fuel_used(size_t i) const { return gx_[i].drive.fuel.economy_used.host(); }
    void set_fuel(size_t i, gt3r::ee::F liters);
    float tread(size_t i, int wheel) const { return cars_[i].w[size_t(wheel)].wear.host(); }
    float wear_grip(size_t i, int wheel) const { return cars_[i].w[size_t(wheel)].wear_grip.host(); }
    const rules::WearRules& wear_rules() const { return tables_.wear; }
    void new_tire(size_t i, int wheel);
    // The flaps' random stream (the program's 0x874D50), for gt4r_diff's whole step.
    void set_random_state(u32 state) { random_state_ = state; }

private:
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
    };
    void place(size_t i, const course::Runway::Place& at);
    void ground_contact(gt3r::sim::exact::Car& c, Extra& x);
    void chassis(size_t i);
    void collide();
    void publish(size_t i);

    std::shared_ptr<const course::Runway> road_;
    std::unique_ptr<RoadGround> ground_;
    std::unique_ptr<gt3r::sim::exact::ProgramData> data_;
    gt3r::sim::exact::RaceInputs race_;
    std::vector<gt3r::sim::exact::Car> cars_;
    std::unique_ptr<collide::Walls> walls_;
    std::vector<collide::Body> bodies_;
    std::vector<collide::CarIn> body_cars_;
    std::vector<Extra> extra_;
    std::vector<sim::Extra> gx_;
    std::vector<rules::WheelLayout> layouts_;
    std::vector<rules::SpeedTables> speed_tables_; // (0x35DE20's, per car: its special row)
    std::vector<std::optional<gt3r::sim::exact::Aids>> driver_aids_; // a computer driver's aid block per car
    std::vector<std::optional<std::array<gt3r::ee::F, 3>>> driver_grip_; // and its grip by tire class
    rules::AidRace aid_race_;
    rules::Tables tables_;
    bool gt4_ready_ = false, gt4_rules_ = false;
    std::vector<Spec> specs_;
    std::vector<race::CarState> states_;
    std::vector<std::string> notes_;
    gt3r::Vu0Random rng_;
    u32 random_state_ = 0; // (the program's shared random stream, 0x874D50, for the flaps: ours from 0)
    bool loose_ = false;
    float pending_ = 0;
    u64 steps_ = 0;
};

} // namespace gt4r::sim
