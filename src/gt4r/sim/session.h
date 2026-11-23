#pragma once

// A session's cars stepped on a course: race::Simulation over the shared engine's vehicle step with
// Gran Turismo 4's cars, tables and road (docs/specs/physics.md "What runs where"). One step is 1/60 s.

#include "gt4r/race/simulation.h"
#include "gt4r/sim/car_spec.h"
#include "gt4r/sim/ground.h"
#include "sim/collision_exact.h"
#include "sim/exact/tick.h"

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
    // The course is a loose one (its first start place is on a loose surface).
    bool loose() const { return loose_; }
    // What the session runs on stand-ins for ("the disc's program is not a build whose tables are mapped").
    const std::vector<std::string>& notes() const { return notes_; }

private:
    struct Extra { // per car, outside the record
        int checkpoint = -1; // distance_along's hint
        float last_v = 0;
        bool has_v = false;
        u8 kinds[4] = {};    // each wheel's road triangle flags
        u8 prev_buttons = 0;
        float spin[4] = {};       // each wheel's angle rolled
        float rest_drop[4] = {};  // each wheel's center below its suspension point, at rest
        bool rest_set[4] = {};
    };
    void place(size_t i, const course::Runway::Place& at);
    void ground_contact(gt3r::sim::exact::Car& c, Extra& x);
    void chassis(gt3r::sim::exact::Car& c, Extra& x);
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
    std::vector<Spec> specs_;
    std::vector<race::CarState> states_;
    std::vector<std::string> notes_;
    gt3r::Vu0Random rng_;
    bool loose_ = false;
    float pending_ = 0;
    u64 steps_ = 0;
};

} // namespace gt4r::sim
