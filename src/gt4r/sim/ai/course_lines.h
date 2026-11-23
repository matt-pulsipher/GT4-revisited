#pragma once

// A course's driving lines as the race prepares them for the computer drivers (0x46A510; docs/specs/ai.md
// "The lines"): each point's road height, surface, slope and bank, its distance along the course, the
// stretch to the next point; and the lines' queries: a line's place at a distance along the course, the
// fallback for a missing line, the lines beside a line.
//
// Positions are in the simulation's frame (x, y across the ground, h the height); the course's frame
// has (x, h, -y).

#include "gt4r/formats/course/ai_line.h"
#include "gt4r/sim/ai/tables.h"
#include "gt4r/sim/ground.h"

#include <array>
#include <vector>

namespace gt4r::sim::ai {

struct LinePoint {
    u8 kind = 0;          // 0 straight, 1 an arc to the left (radius > 0), 2 to the right
    s8 brake_adjust = 0;  // percent
    s8 speed_adjust = 0;  // km/h; -128: the point is passed over for the speeds
    float x = 0, y = 0;
    float height = 0;     // the road's
    int surface = 0;      // the road's surface index (ground.h surface_index)
    bool pit = false;     // on the pit lane's surface
    float dist = 0;       // along the course (the stop line: a lap added from its first point in the lap's first half)
    float radius = 0;
    float slope = 0, bank = 0; // the road's slope along the heading (+ uphill) and its bank across it (+ the left side
                               // higher), radians
    float heading = 0;
    float length = 0;     // to the next point along the line (the last: to the first)
    float rise = 0;       // the next point's height less this one's
    float pitch = 0;      // atan2(rise, length)
    bool crest = false;   // the pitch falls by more than a degree here: no downforce bonus in the arc from here
};

class CourseLines {
public:
    static constexpr int kMain = 6, kPitLane = 3, kPitStop = 4, kSideMap = 5;

    CourseLines() = default;
    CourseLines(const course::AiData& data, const RoadGround& ground, const Tables& tables);

    float lap() const { return lap_; }
    bool empty() const;
    // Line k's points; empty: the line is absent (or dropped: a passing line whose distances do not rise).
    const std::vector<LinePoint>& line(int k) const { return lines_[size_t(k) & 7]; }
    bool has(int k) const { return k >= 0 && k < 8 && !lines_[size_t(k)].empty(); }
    // The first line of the request's fallback row that is present (a request outside 0..7 asks for 6); -1 none.
    int resolve(int k) const;
    // The lines beside line k (0x46BC00 / 0x46BC40; -1: none): left of 1 the main line 6 (2 without it), left of 6
    // line 2; right of 2 the main line (1 without it), right of 6 line 1. Line 2 is the left passing line, 1 the right.
    int left(int k) const;
    int right(int k) const;

    // Line k's place at distance s along the course (wrapped into the lap), searching from point `hint`:
    // straights interpolate, arcs turn about their center. False when the line has no span there.
    bool evaluate(int k, int hint, float s, float& x, float& y, float& h) const;
    // The line's heading at distance s: towards the first place at least 0.2 m on, in 0.5 m steps.
    float heading_at(int k, int hint, float s) const;
    // The course distance of a simulation-frame point (the road's checkpoints), false off the course.
    bool distance(float x, float y, float h, float& s, int* hint = nullptr) const;

    // A course distance wrapped once into the lap (the road's checkpoints run a little past it around the line).
    float wrap_lap(float s) const;
    const RoadGround* ground() const { return ground_; }

private:
    void prepare(int k, float& query_height);
    int span(int k, int hint, float s, int& from, int& to) const;

    const RoadGround* ground_ = nullptr;
    float lap_ = 0;
    std::array<std::vector<LinePoint>, 8> lines_;
    std::array<std::array<s8, 6>, 8> fallback_{};
};

} // namespace gt4r::sim::ai
