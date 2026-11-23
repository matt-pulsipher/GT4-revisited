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
    float brake = 1;          // +0x00 the braking factor: above 1 brakes later
    float corner_scale = 1;   // +0x04 the arcs' speeds' scale
    float corner_offset = 0;  // +0x08 and offset (m/s)
    float yaw_gain = 2;       // +0x0C the wanted slip angle per rad/s of yaw rate short
    float sat_throttle = 1, sat_brake = 0; // +0x10 / +0x14 the throttle cut and brake past the tires' peak
    float look_speed = 0;     // +0x18 1 / the speed of the longest look ahead
    float look_min = 15, look_max = 40; // +0x1C / +0x20 the look ahead at rest and at that speed (m)
    float front_weight = 1;   // +0x58 the front axle's share of the arcs' grip
    float corner_bonus = 0;   // +0x5C added to an arc's speed after bonus_ticks in it (m/s)
    float steer_rate = 1.57f; // +0x60 the steering's slew (rad/s)
    float pit_speed = 22.2f;  // +0x64 (m/s)
    int bonus_ticks = 0;      // +0x68
    float throttle = 1;       // +0x6C the event's throttle share
    float downforce = 0;      // +0x70 the lift coefficients over the mass
    float aero_bonus = 0;     // +0x74
    bool straight_bonus = false; // +0x78 the arcs' speed raised while the heading error is small
    float bonus_angle = 0, bonus_speed = 0; // +0x7C (rad) / +0x80 (m/s)
    std::array<float, 3> grip{1, 1, 1}; // +0xA8 the arcs' grip by tire class
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

class Field {
public:
    // The drivers of `computer`'s cars of a session on a course whose driver file is `data`.
    Field(const Session& session, const course::AiData& data, const Program& program, std::span<const size_t> computer, const Pace& pace = {});
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
    };
    Snapshot snapshot(size_t car) const;
    // The rubber band's time scale for a car (1: none). The session does not apply it (docs/specs/ai.md).
    float time_scale(size_t car) const;

private:
    void proximity();
    void rubber_band();
    void decide(Driver& d);
    float braking(Driver& d);
    void bookkeeping_shift(Driver& d);
    void replan(Driver& d);
    float corner_speed(const Driver& d, int i);
    void speed_control(Driver& d, float target, float gain);
    void traffic(Driver& d);
    bool request_line(Driver& d, int line, bool at_once);
    void target(const Driver& d, int line, float look, float& dx, float& dy) const;
    void incidents_after(Driver& d);

    const Session* session_;
    Tables tables_;
    CourseLines lines_;
    Pace pace_;
    std::vector<std::unique_ptr<Driver>> drivers_; // by car index (null: not driven)
    std::vector<int> ranks_;
    u64 steps_ = 0;
    std::array<u32, 24> soft_{}; // a material's soft flag (the program's table)
};

} // namespace gt4r::sim::ai
