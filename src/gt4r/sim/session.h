#pragma once

// A session's cars stepped on a course: race::Simulation over the shared engine's vehicle step with
// Gran Turismo 4's cars, tables and road (docs/specs/physics.md "What runs where"). One step is 1/60 s.

#include "gt4r/race/simulation.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/ground.h"
#include "gt4r/sim/record.h"
#include "gt4r/sim/rules_step.h"
#include "sim/collision_exact.h"
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
    // What the session runs on stand-ins for ("the disc's program is not a build whose tables are mapped").
    const std::vector<std::string>& notes() const { return notes_; }

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
    class Walls;
    std::unique_ptr<Walls> walls_;
    std::vector<gt3r::sim::collide::exact::Body> bodies_;
    std::vector<Extra> extra_;
    std::vector<sim::Extra> gx_;
    std::vector<rules::WheelLayout> layouts_;
    rules::Tables tables_;
    bool gt4_ready_ = false, gt4_rules_ = false;
    std::vector<Spec> specs_;
    std::vector<race::CarState> states_;
    std::vector<std::string> notes_;
    gt3r::Vu0Random rng_;
    bool loose_ = false;
    float pending_ = 0;
    u64 steps_ = 0;
};

} // namespace gt4r::sim
