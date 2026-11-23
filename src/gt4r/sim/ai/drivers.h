#pragma once

// The computer drivers (docs/specs/ai.md): per car, from its state and the others' to a race::CarInput each
// step. They follow the course's driving lines at the speeds the car's tires allow, brake for the arcs, keep
// apart from and pass the cars around them, and turn round or back out after a spin or a wall.
//
//   ai::Field field = ai::Field::open(disc, "tsukuba", session, {1, 2, 3, 4, 5});  // cars 1.. are the computer's
//   std::vector<race::CarInput> in(session.cars().size());
//   while (countdown) { field.hold(in); session.hold_once(in); }
//   for (;;) { /* in[0] = the player's pad */ field.drive(in); session.step_once(in); }
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
};

// The race's own settings for its computer drivers.
struct Pace {
    // The event's percents per driveline class (RACE row: throttle, braking, arcs' speeds; 0: 100).
    std::array<u8, 4> throttle{}, brake{}, corner{};
    // The rubber band (RACE row): the cars ahead of the reference slowed, those behind hurried, by up to these shares
    // past `start` metres, fully past `full` metres. All 0: none.
    float slow = 0, slow_start = 0, slow_full = 0, fast = 0, fast_start = 0, fast_full = 0, global = 1;
    bool standing_start = true; // the long look ahead off the grid of a standing start
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
    void end_step() { ++steps_; }

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
    // The rubber band's time scale for a car (1: none). The session does not apply it (docs/specs/ai.md).
    float time_scale(size_t car) const;

    // For checks against another race's drivers: a driver's plan and traffic state set as that race's driver object
    // holds them, and the plan's routines run on it.
    struct Probe {
        int line = 6, next = 0, mode = 0, next_kind = 0, status = 0, follow = -1, chain = 0;
        gt3r::ee::F v_cur, v_next;
        float contact = 0, s = 0;
        u16 rand = 0;
        gt3r::ee::F corner_mult{1.0f}, brake_mult{1.0f}, alongside_mult{1.0f};
        std::array<u8, 8> zone{};
        s16 cooldown = 0;
    };
    void probe_set(size_t car, const Probe& p);
    Probe probe_get(size_t car) const;
    // 0x4674F0: the speed of line point i of the car's line.
    gt3r::ee::F probe_corner_speed(size_t car, int i);
    // 0x463EA8: the point bookkeeping and the braking; the throttle multiplier (the state changes as the routine's).
    gt3r::ee::F probe_braking(size_t car);

private:
    void proximity();
    void rubber_band();
    void decide(Driver& d);
    gt3r::ee::F braking(Driver& d);
    void bookkeeping_shift(Driver& d);
    void replan(Driver& d);
    gt3r::ee::F corner_speed(const Driver& d, int i);
    gt3r::ee::F ai_grip(const Driver& d, int surface) const;
    void speed_control(Driver& d, float target, float gain);
    void traffic(Driver& d);
    bool request_line(Driver& d, int line, bool at_once);
    void target(const Driver& d, int line, float look, float& dx, float& dy) const;
    void incidents_after(Driver& d);

    void init(const course::AiData& data, const Program& program, std::span<const size_t> computer);

    std::unique_ptr<SessionCars> own_cars_;
    const Cars* session_;
    Tables tables_;
    CourseLines lines_;
    Pace pace_;
    std::vector<std::unique_ptr<Driver>> drivers_; // by car index (null: not driven)
    std::vector<int> ranks_;
    u64 steps_ = 0;
    std::array<u32, 24> soft_{}; // a material's soft flag (the program's table)
};

} // namespace gt4r::sim::ai
