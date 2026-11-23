#pragma once

// A stand-in Simulation for building and testing the race view: every car runs along the
// middle line of the road's checkpoints. Throttle and brake set its speed, steering moves it
// across the road, the body leans and dips on four sprung wheels that turn. Nothing here is
// the game's physics; the real simulation replaces it behind the same interface
// (simulation.h).

#include "gt4r/race/simulation.h"

#include <memory>
#include <vector>

namespace gt4r::race {

// A car's sizes for the stand-in (metres). Index 0 front, 1 rear.
struct RailCar {
    float wheelbase = 2.6f;
    float half_track[2] = {0.75f, 0.75f};
    float tire_radius[2] = {0.32f, 0.32f};
    float top_speed = 70.0f;    // metres per second
    float acceleration = 6.0f;  // metres per second squared from rest
    float rev_limit = 7500.0f;
    int gears = 5;
};

// The line the cars follow: the checkpoints' middle points in course order, moved to the middle
// of the paved strip under each, with the room to either side.
struct RailLine {
    struct Point {
        Vec3 pos;        // on the road
        Vec3 across;     // unit vector to the right of the direction of travel, level
        float v = 0;     // metres along the course
        float left = 0, right = 0; // paved room to the left and right of `pos`, metres
    };
    std::vector<Point> points; // ascending v; the line closes from the last to the first
    float length = 0;          // of a lap

    explicit RailLine(const course::Runway& road);
    RailLine() = default;
    bool valid() const { return points.size() >= 3 && length > 0; }

    struct Sample {
        Vec3 pos, tangent, across; // unit vectors; tangent along the direction of travel
        float left = 0, right = 0;
        float curvature = 0;       // 1 / metres, + turning right
    };
    // The line at `v` metres along the course (any value: it wraps).
    Sample at(float v) const;
    // The place on the line nearest a point: metres along and metres to the right of it.
    void project(const Vec3& p, float& v, float& offset) const;
};

class RailSimulation : public Simulation {
public:
    // `cars`: one entry per car, placed on the road's start grid in order (places past the grid's
    // last are lined up behind it).
    RailSimulation(const course::Runway& road, std::vector<RailCar> cars);

    void step(std::span<const CarInput> inputs, float dt) override;
    std::span<const CarState> cars() const override { return states_; }

    const RailLine& line() const { return line_; }

private:
    struct Car {
        RailCar spec;
        float v = 0;       // metres along the course
        float offset = 0;  // metres to the right of the line
        float speed = 0;
        float lateral_speed = 0;
        float yaw = 0;     // of the body against the line's direction, radians
        // The body on its springs: pitch (+ nose up), roll (+ right side down), heave, and their rates.
        float pitch = 0, pitch_rate = 0, roll = 0, roll_rate = 0, heave = 0, heave_rate = 0;
        float spin[4] = {};
        float last_ground[4] = {};
        bool has_ground = false;
        int gear = 1;
        bool shift_held = false;
    };
    void place(Car& c, CarState& s, float long_accel, float lat_accel, const CarInput& in, float dt) const;

    const course::Runway* road_;
    RailLine line_;
    std::vector<Car> cars_;
    std::vector<CarState> states_;
};

// A simple driver for any car on any Simulation: steers toward the road's line and slows for
// the bends ahead. `offset`: metres to the right of the line it aims for; `pace`: 0..1.
CarInput follow_line(const RailLine& line, const CarState& car, float offset = 0, float pace = 1);

} // namespace gt4r::race
